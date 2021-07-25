#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace coro
{
class io_scheduler;

template<concepts::executor executor_type>
class task_container
{
public:
    using task_position = std::list<std::size_t>::iterator;

    struct options
    {
        /// The number of task spots to reserve space for upon creating the container.
        std::size_t reserve_size{8};
        /// The growth factor for task space in the container when capacity is full.
        double growth_factor{2};
    };

    /**
     * @param e Tasks started in the container are scheduled onto this executor.  For tasks created
     *           from a coro::io_scheduler, this would usually be that coro::io_scheduler instance.
     * @param opts Task container options.
     */
    task_container(
        std::shared_ptr<executor_type> e, const options opts = options{.reserve_size = 8, .growth_factor = 2})
        : m_growth_factor(opts.growth_factor),
          m_executor(std::move(e)),
          m_executor_ptr(m_executor.get())
    {
        if (m_executor == nullptr)
        {
            throw std::runtime_error{"task_container cannot have a nullptr executor"};
        }

        init(opts.reserve_size);
    }
    task_container(const task_container&) = delete;
    task_container(task_container&&)      = delete;
    auto operator=(const task_container&) -> task_container& = delete;
    auto operator=(task_container &&) -> task_container& = delete;
    ~task_container()
    {
        // This will hang the current thread.. but if tasks are not complete thats also pretty bad.
        while (!empty())
        {
            garbage_collect();
        }
    }

    enum class garbage_collect_t
    {
        /// Execute garbage collection.
        yes,
        /// Do not execute garbage collection.
        no
    };

    /**
     * Stores a user task and starts its execution on the container's thread pool.
     * @param user_task The scheduled user's task to store in this task container and start its execution.
     * @param cleanup Should the task container run garbage collect at the beginning of this store
     *                call?  Calling at regular intervals will reduce memory usage of completed
     *                tasks and allow for the task container to re-use allocated space.
     */
    auto start(coro::task<void>&& user_task, garbage_collect_t cleanup = garbage_collect_t::yes) -> void
    {
        m_size.fetch_add(1, std::memory_order::relaxed);

        std::scoped_lock lk{m_mutex};

        if (cleanup == garbage_collect_t::yes)
        {
            gc_internal();
        }

        // Only grow if completely full and attempting to add more.
        if (m_free_pos == m_task_indexes.end())
        {
            m_free_pos = grow();
        }

        // Store the task inside a cleanup task for self deletion.
        auto index     = *m_free_pos;
        m_tasks[index] = make_cleanup_task(std::move(user_task), m_free_pos);

        // Mark the current used slot as used.
        std::advance(m_free_pos, 1);

        // Start executing from the cleanup task to schedule the user's task onto the thread pool.
        m_tasks[index].resume();
    }

    /**
     * Garbage collects any tasks that are marked as deleted.  This frees up space to be re-used by
     * the task container for newly stored tasks.
     * @return The number of tasks that were deleted.
     */
    auto garbage_collect() -> std::size_t __attribute__((used))
    {
        std::scoped_lock lk{m_mutex};
        return gc_internal();
    }

    /**
     * @return The number of tasks that are awaiting deletion.
     */
    auto delete_task_size() const -> std::size_t
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_tasks_to_delete.size();
    }

    /**
     * @return True if there are no tasks awaiting deletion.
     */
    auto delete_tasks_empty() const -> bool
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_tasks_to_delete.empty();
    }

    /**
     * @return The number of active tasks in the container.
     */
    auto size() const -> std::size_t { return m_size.load(std::memory_order::relaxed); }

    /**
     * @return True if there are no active tasks in the container.
     */
    auto empty() const -> bool { return size() == 0; }

    /**
     * @return The capacity of this task manager before it will need to grow in size.
     */
    auto capacity() const -> std::size_t
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_tasks.size();
    }

    /**
     * Will continue to garbage collect and yield until all tasks are complete.  This method can be
     * co_await'ed to make it easier to wait for the task container to have all its tasks complete.
     *
     * This does not shut down the task container, but can be used when shutting down, or if your
     * logic requires all the tasks contained within to complete, it is similar to coro::latch.
     */
    auto garbage_collect_and_yield_until_empty() -> coro::task<void>
    {
        while (!empty())
        {
            garbage_collect();
            co_await m_executor_ptr->yield();
        }
    }

private:
    /**
     * Grows each task container by the growth factor.
     * @return The position of the free index after growing.
     */
    auto grow() -> task_position
    {
        // Save an index at the current last item.
        auto        last_pos = std::prev(m_task_indexes.end());
        std::size_t new_size = m_tasks.size() * m_growth_factor;
        for (std::size_t i = m_tasks.size(); i < new_size; ++i)
        {
            m_task_indexes.emplace_back(i);
        }
        m_tasks.resize(new_size);
        // Set the free pos to the item just after the previous last item.
        return std::next(last_pos);
    }

    /**
     * Interal GC call, expects the public function to lock.
     */
    auto gc_internal() -> std::size_t
    {
        std::size_t deleted{0};
        if (!m_tasks_to_delete.empty())
        {
            for (const auto& pos : m_tasks_to_delete)
            {
                // This doesn't actually 'delete' the task, it'll get overwritten when a
                // new user task claims the free space.  It could be useful to actually
                // delete the tasks so the coroutine stack frames are destroyed.  The advantage
                // of letting a new task replace and old one though is that its a 1:1 exchange
                // on delete and create, rather than a large pause here to delete all the
                // completed tasks.

                // Put the deleted position at the end of the free indexes list.
                m_task_indexes.splice(m_task_indexes.end(), m_task_indexes, pos);
            }
            deleted = m_tasks_to_delete.size();
            m_tasks_to_delete.clear();
        }
        return deleted;
    }

    /**
     * Encapsulate the users tasks in a cleanup task which marks itself for deletion upon
     * completion.  Simply co_await the users task until its completed and then mark the given
     * position within the task manager as being deletable.  The scheduler's next iteration
     * in its event loop will then free that position up to be re-used.
     *
     * This function will also unconditionally catch all unhandled exceptions by the user's
     * task to prevent the scheduler from throwing exceptions.
     * @param user_task The user's task.
     * @param pos The position where the task data will be stored in the task manager.
     * @return The user's task wrapped in a self cleanup task.
     */
    auto make_cleanup_task(task<void> user_task, task_position pos) -> coro::task<void>
    {
        // Immediately move the task onto the executor.
        co_await m_executor_ptr->schedule();

        try
        {
            // Await the users task to complete.
            co_await user_task;
        }
        catch (const std::exception& e)
        {
            // TODO: what would be a good way to report this to the user...?  Catching here is required
            // since the co_await will unwrap the unhandled exception on the task.
            // The user's task should ideally be wrapped in a catch all and handle it themselves, but
            // that cannot be guaranteed.
            std::cerr << "coro::task_container user_task had an unhandled exception e.what()= " << e.what() << "\n";
        }
        catch (...)
        {
            // don't crash if they throw something that isn't derived from std::exception
            std::cerr << "coro::task_container user_task had unhandle exception, not derived from std::exception.\n";
        }

        std::scoped_lock lk{m_mutex};
        m_tasks_to_delete.push_back(pos);
        // This has to be done within scope lock to make sure this coroutine task completes before the
        // task container object destructs -- if it was waiting on .empty() to become true.
        m_size.fetch_sub(1, std::memory_order::relaxed);
        co_return;
    }

    /// Mutex for safely mutating the task containers across threads, expected usage is within
    /// thread pools for indeterminate lifetime requests.
    std::mutex m_mutex{};
    /// The number of alive tasks.
    std::atomic<std::size_t> m_size{};
    /// Maintains the lifetime of the tasks until they are completed.
    std::vector<task<void>> m_tasks{};
    /// The full set of indexes into `m_tasks`.
    std::list<std::size_t> m_task_indexes{};
    /// The set of tasks that have completed and need to be deleted.
    std::vector<task_position> m_tasks_to_delete{};
    /// The current free position within the task indexes list.  Anything before
    /// this point is used, itself and anything after is free.
    task_position m_free_pos{};
    /// The amount to grow the containers by when all spaces are taken.
    double m_growth_factor{};
    /// The executor to schedule tasks that have just started.
    std::shared_ptr<executor_type> m_executor{nullptr};
    /// This is used internally since io_scheduler cannot pass itself in as a shared_ptr.
    executor_type* m_executor_ptr{nullptr};

    /**
     * Special constructor for internal types to create their embeded task containers.
     */

    friend io_scheduler;
    task_container(executor_type& e, const options opts = options{.reserve_size = 8, .growth_factor = 2})
        : m_growth_factor(opts.growth_factor),
          m_executor_ptr(&e)
    {
        init(opts.reserve_size);
    }

    auto init(std::size_t reserve_size) -> void
    {
        m_tasks.resize(reserve_size);
        for (std::size_t i = 0; i < reserve_size; ++i)
        {
            m_task_indexes.emplace_back(i);
        }
        m_free_pos = m_task_indexes.begin();
    }
};

} // namespace coro

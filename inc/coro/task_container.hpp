#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <list>
#include <mutex>
#include <vector>

namespace coro
{
class thread_pool;

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
     * @param tp Tasks started in the container are scheduled onto this thread pool.  For tasks created
     *           from a coro::io_scheduler, this would usually be that coro::io_scheduler instance.
     * @param opts Task container options.
     */
    task_container(thread_pool& tp, const options opts = options{.reserve_size = 8, .growth_factor = 2});
    task_container(const task_container&) = delete;
    task_container(task_container&&)      = delete;
    auto operator=(const task_container&) -> task_container& = delete;
    auto operator=(task_container&&) -> task_container& = delete;
    ~task_container();

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
    auto start(coro::task<void> user_task, garbage_collect_t cleanup = garbage_collect_t::yes) -> void;

    /**
     * Garbage collects any tasks that are marked as deleted.  This frees up space to be re-used by
     * the task container for newly stored tasks.
     * @return The number of tasks that were deleted.
     */
    auto garbage_collect() -> std::size_t;

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
    auto garbage_collect_and_yield_until_empty() -> coro::task<void>;

private:
    /**
     * Grows each task container by the growth factor.
     * @return The position of the free index after growing.
     */
    auto grow() -> task_position;

    /**
     * Interal GC call, expects the public function to lock.
     */
    auto gc_internal() -> std::size_t;

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
    auto make_cleanup_task(task<void> user_task, task_position pos) -> coro::task<void>;

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
    /// The thread pool to schedule tasks that have just started.
    thread_pool& m_thread_pool;
};

} // namespace coro

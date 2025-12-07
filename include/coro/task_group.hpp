#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/concepts/range_of.hpp"
#include "coro/detail/task_self_deleting.hpp"
#include "coro/event.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace coro
{

template<concepts::executor executor_type>
class task_group
{
public:
    /**
     * Creates a task group to run the tasks on the given executor, use start(task) to add tasks to the group.
     * @param executor The executor to run the tasks on.
     * @throws std::runtime_error If the provided executor is nullptr.
     */
    explicit task_group(executor_type* executor)
        : m_executor(executor)
    {
        if (executor == nullptr)
        {
            throw std::runtime_error{"task_group cannot have a nullptr executor"};
        }
    }

    /**
     * Creates a task group with a single task to start.
     * @param executor The executor to run the tasks on.
     * @param task The initial starting task to start in the group, can use start(task) to add more to the group.
     */
    explicit task_group(executor_type* executor, coro::task<void>&& task)
        : task_group(executor)
    {
        (void)start(std::forward<coro::task<void>>(task));
    }

    /**
     * Creates a task group with the given range of tasks to start.
     * @tparam range_type The range type of tasks.
     * @param executor The executor to run the tasks on.
     * @param tasks The initial starting set of tasks to start in the group, can use start(task) to add more to the group.
     */
    template<coro::concepts::range_of<coro::task<void>> range_type>
    explicit task_group(executor_type* executor, range_type tasks) : task_group(executor)
    {
        for (auto& task : tasks)
        {
            (void)start(std::move(task));
        }
    }

    explicit task_group(std::unique_ptr<executor_type>& executor)
        : m_executor(executor.get())
    { }

    explicit task_group(std::unique_ptr<executor_type>& executor, coro::task<void>&& task)
        : task_group(executor.get(), std::forward<coro::task<void>>(task))
    { }

    template<coro::concepts::range_of<coro::task<void>> range_type>
    explicit task_group(std::unique_ptr<executor_type>& executor, range_type tasks) : task_group(executor.get(), std::forward<range_type>(tasks))
    { }

    task_group(const task_group&)                    = delete;
    task_group(task_group&&)                         = delete;
    auto operator=(const task_group&) -> task_group& = delete;
    auto operator=(task_group&&) -> task_group&      = delete;

    /**
     * When the task group destructs it waits for all of its tasks to complete. It is advisable to
     * use `co_await task_group` to efficiently wait for all tasks to complete as
     * destructors cannot be co_await'ed this will hang the calling thread until they are done.
     */
    ~task_group()
    {
        while (!empty())
        {
            // Sleep a bit so the cpu doesn't totally churn.
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }

    /**
     * Starts a task into this group.
     * @param task The task to include into this group.
     * @return True if the task was started, this will only return false if the executor
     *         has been shutdown.
     */
    [[nodiscard]] auto start(coro::task<void>&& task) -> bool
    {
        // Make sure the event isn't triggered, or can trigger again.
        m_on_empty_event.reset();
        m_size.fetch_add(1, std::memory_order::release);
        auto wrapper_task = detail::make_task_self_deleting(std::move(task));
        wrapper_task.promise().user_final_suspend([this]() -> void { count_down(); });

        // Kick it.
        if (!m_executor->resume(wrapper_task.handle()))
        {
            count_down();
            return false;
        }
        return true;
    }

    /**
     * @return The number of active tasks in the group.
     */
    [[nodiscard]] auto size() const -> std::size_t { return m_size.load(std::memory_order::acquire); }

    /**
     * @return True if there are no active tasks in the group.
     */
    [[nodiscard]] auto empty() const -> bool { return size() == 0; }

    /**
     * Wait until the task group's tasks are completed (zero tasks remain).
     * NOTE: that this can happen multiple times if you are using start(task) as its possible
     * for the task count to drop to zero and then increase again. It is advisable to not
     * await this method on the task group until you know all tasks in the group have started.
     */
    auto operator co_await() const noexcept -> event::awaiter { return m_on_empty_event.operator co_await(); }

private:
    executor_type* m_executor{nullptr};
    /// The number of alive tasks.
    std::atomic<uint64_t> m_size{};
    /// Event to trigger if m_size goes to zero.
    coro::event m_on_empty_event{};

    auto count_down() -> void
    {
        if (m_size.fetch_sub(1, std::memory_order::acq_rel) == 1)
        {
            m_on_empty_event.set();
        }
    }
};

} // namespace coro

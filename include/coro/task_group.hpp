#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/detail/task_self_deleting.hpp"
#include "coro/event.hpp"
#include "coro/latch.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <memory>
#include <ranges>
#include <thread>

namespace coro
{
class io_scheduler;

template<concepts::executor executor_type>
class task_group
{
public:
    /**
     * @tparam range_type The range type.
     * @param executor Tasks started in the group are scheduled onto this executor.
     * @param tasks The group of tasks to track.
     */
    template<coro::concepts::range_of<coro::task<void>> range_type>
    explicit task_group(std::unique_ptr<executor_type>& executor, range_type tasks) : m_size(std::size(tasks))
    {
        if (executor == nullptr)
        {
            throw std::runtime_error{"task_group cannot have a nullptr executor"};
        }

        for (auto& task : tasks)
        {
            auto wrapper_task = detail::make_task_self_deleting(std::move(task));
            wrapper_task.promise().user_final_suspend([this]() -> void { m_size.count_down(); });
            if (!executor->resume(wrapper_task.handle()))
            {
                m_size.count_down();
            }
        }
    }
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
     * @return The number of active tasks in the group.
     */
    [[nodiscard]] auto size() const -> std::size_t { return m_size.remaining(); }

    /**
     * @return True if there are no active tasks in the group.
     */
    [[nodiscard]] auto empty() const -> bool { return size() == 0; }

    /**
     * Wait until the task group's tasks are completed (zero tasks remain).
     */
    auto operator co_await() const noexcept -> event::awaiter { return m_size.operator co_await(); }

private:
    /// The number of alive tasks.
    coro::latch m_size;
};

} // namespace coro

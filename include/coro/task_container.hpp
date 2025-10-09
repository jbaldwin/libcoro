#pragma once

#include "coro/attribute.hpp"
#include "coro/concepts/executor.hpp"
#include "coro/detail/task_self_deleting.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace coro
{
class io_scheduler;

template<concepts::executor executor_type>
class task_container
{
public:
    /**
     * @param e Tasks started in the container are scheduled onto this executor.  For tasks created
     *           from a coro::io_scheduler, this would usually be that coro::io_scheduler instance.
     */
    explicit task_container(std::unique_ptr<executor_type>& e) : m_executor(e.get())
    {
        if (m_executor == nullptr)
        {
            throw std::runtime_error{"task_container cannot have a nullptr executor"};
        }
    }
    task_container(const task_container&)                    = delete;
    task_container(task_container&&)                         = delete;
    auto operator=(const task_container&) -> task_container& = delete;
    auto operator=(task_container&&) -> task_container&      = delete;
    ~task_container()
    {
        // This will hang the current thread.. but if tasks are not complete thats also pretty bad.
        while (!empty())
        {
            // Sleep a bit so the cpu doesn't totally churn.
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }

    /**
     * Stores a user task and starts its execution on the container's thread pool.
     * @param user_task The scheduled user's task to store in this task container and start its execution.
     * @return True if the task was succesfully started into the task container. This can fail if the task
     *         is already completed or does not contain a valid coroutine anymore.
     */
    auto start(coro::task<void>&& user_task) -> bool
    {
        m_size.fetch_add(1, std::memory_order::relaxed);

        auto task = make_self_deleting_task(std::move(user_task));
        // Hook the promise to decrement the size upon its self deletion of the coroutine frame.
        task.promise().executor_size(m_size);
        return m_executor->resume(task.handle());
    }

    /**
     * @return The number of active tasks in the container.
     */
    [[nodiscard]] auto size() const -> std::size_t { return m_size.load(std::memory_order::acquire); }

    /**
     * @return True if there are no active tasks in the container.
     */
    [[nodiscard]] auto empty() const -> bool { return size() == 0; }

    /**
     * Will continue to yield until all tasks are complete.  This method can be
     * co_await'ed to make it easier to wait for the task container to have all its tasks complete.
     *
     * This does not shut down the task container, but can be used when shutting down, or if your
     * logic requires all the tasks contained within to complete, it is similar to coro::latch.
     */
    auto yield_until_empty() -> coro::task<void>
    {
        while (!empty())
        {
            co_await m_executor->yield();
        }
    }

private:
    static auto make_self_deleting_task(task<void> user_task) -> detail::task_self_deleting
    {
        co_await user_task;
        co_return;
    }

    /// The number of alive tasks.
    std::atomic<std::size_t> m_size{};
    /// The executor to schedule tasks that have just started.
    executor_type* m_executor{nullptr};
};

} // namespace coro

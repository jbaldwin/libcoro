#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <coroutine>
#include <cstdint>

namespace coro::detail
{

class task_self_deleting;

class promise_self_deleting
{
public:
    promise_self_deleting();
    ~promise_self_deleting();

    promise_self_deleting(const promise_self_deleting&) = delete;
    promise_self_deleting(promise_self_deleting&&);
    auto operator=(const promise_self_deleting&) -> promise_self_deleting& = delete;
    auto operator=(promise_self_deleting&&) -> promise_self_deleting&;

    auto get_return_object() -> task_self_deleting;
    auto initial_suspend() -> std::suspend_always;
    auto final_suspend() noexcept -> std::suspend_never;
    auto return_void() noexcept -> void;
    auto unhandled_exception() -> void;

    auto executor_size(std::atomic<std::size_t>& task_container_size) -> void;

private:
    /**
     * The executor m_size member to decrement upon the coroutine completing.
     */
    std::atomic<std::size_t>* m_executor_size{nullptr};
};

/**
 * This task will self delete upon completing. This is useful for usecase that the lifetime of the
 * coroutine cannot be determined and it needs to 'self' delete. This is achieved by returning
 * std::suspend_never from the promise::final_suspend which then based on the spec tells the
 * coroutine to delete itself. This means any classes that use this task cannot have owning
 * pointers or relationships to this class and must not use it past its completion.
 *
 * This class is currently only used by coro::task_container<executor_t> and will decrement its
 * m_size internal count when the coroutine completes.
 */
class task_self_deleting
{
public:
    using promise_type = promise_self_deleting;

    explicit task_self_deleting(promise_self_deleting& promise);
    ~task_self_deleting();

    task_self_deleting(const task_self_deleting&) = delete;
    task_self_deleting(task_self_deleting&&);
    auto operator=(const task_self_deleting&) -> task_self_deleting& = delete;
    auto operator=(task_self_deleting&&) -> task_self_deleting&;

    auto promise() -> promise_self_deleting& { return *m_promise; }
    auto handle() -> std::coroutine_handle<promise_self_deleting>
    {
        return std::coroutine_handle<promise_self_deleting>::from_promise(*m_promise);
    }

private:
    promise_self_deleting* m_promise{nullptr};
};

/**
 * Turns a coro::task<void> into a self deleting task (detached).
 */
auto make_task_self_deleting(coro::task<void> user_task) -> task_self_deleting;

} // namespace coro::detail

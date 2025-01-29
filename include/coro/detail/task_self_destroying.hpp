#pragma once

#include <atomic>
#include <coroutine>
#include <cstdint>

namespace coro::detail
{

class task_self_destroying;

class promise_self_destroying
{
public:
    promise_self_destroying();
    ~promise_self_destroying();

    promise_self_destroying(const promise_self_destroying&) = delete;
    promise_self_destroying(promise_self_destroying&&);
    auto operator=(const promise_self_destroying&) -> promise_self_destroying& = delete;
    auto operator=(promise_self_destroying&&) -> promise_self_destroying&;

    auto get_return_object() -> task_self_destroying;
    auto initial_suspend() -> std::suspend_always;
    auto final_suspend() noexcept -> std::suspend_never;
    auto return_void() noexcept -> void;
    auto unhandled_exception() -> void;

    auto task_container_size(std::atomic<std::size_t>& task_container_size) -> void;
    /**
     * The coro::task_container<executor_t> m_size member to decrement upon the coroutine completing.
     */
private:
    std::atomic<std::size_t>* m_task_container_size{nullptr};
};

/**
 * This task will self delete upon completing. This is useful for usecase that the lifetime of the
 * coroutine cannot be determined and it needs to 'self' delete. This is achieved by returning
 * std::suspend_never from the promise::final_suspend which then based on the spec tells the
 * coroutine to destroy itself. This means any classes that use this task cannot have owning
 * pointers or relationships to this class and must not use it past its completion.
 *
 * This class is currently only used by coro::task_container<executor_t> and will decrement its
 * m_size internal count when the coroutine completes.
 */
class task_self_destroying
{
public:
    using promise_type = promise_self_destroying;

    explicit task_self_destroying(promise_self_destroying& promise);
    ~task_self_destroying();

    task_self_destroying(const task_self_destroying&) = delete;
    task_self_destroying(task_self_destroying&&);
    auto operator=(const task_self_destroying&) -> task_self_destroying& = delete;
    auto operator=(task_self_destroying&&) -> task_self_destroying&;

    auto promise() -> promise_self_destroying& { return *m_promise; }
    auto handle() -> std::coroutine_handle<promise_self_destroying> { return std::coroutine_handle<promise_self_destroying>::from_promise(*m_promise); }
private:
    promise_self_destroying* m_promise{nullptr};
};

} // namespace coro::detail

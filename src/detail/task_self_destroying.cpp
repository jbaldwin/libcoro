#include "coro/detail/task_self_destroying.hpp"

#include <utility>

namespace coro::detail
{

promise_self_destroying::promise_self_destroying()
{
    (void)m_task_container_size; // make codacy happy
}

promise_self_destroying::~promise_self_destroying()
{

}

promise_self_destroying::promise_self_destroying(promise_self_destroying&& other)
    : m_task_container_size(std::exchange(other.m_task_container_size, nullptr))
{

}

auto promise_self_destroying::operator=(promise_self_destroying&& other) -> promise_self_destroying&
{
    if (std::addressof(other) != nullptr)
    {
        m_task_container_size = std::exchange(other.m_task_container_size, nullptr);
    }

    return *this;
}

auto promise_self_destroying::get_return_object() -> task_self_destroying
{
    return task_self_destroying{*this};
}

auto promise_self_destroying::initial_suspend() -> std::suspend_always
{
    return std::suspend_always{};
}

auto promise_self_destroying::final_suspend() noexcept -> std::suspend_never
{
    // Notify the task_container<executor_t> that this coroutine has completed.
    if (m_task_container_size != nullptr)
    {
        m_task_container_size->fetch_sub(1);
    }

    // By not suspending this lets the coroutine destroy itself.
    return std::suspend_never{};
}

auto promise_self_destroying::return_void() noexcept -> void
{
    // no-op
}

auto promise_self_destroying::unhandled_exception() -> void
{
    // The user cannot access the promise anyways, ignore the exception.
}

auto promise_self_destroying::task_container_size(std::atomic<std::size_t>& task_container_size) -> void
{
    m_task_container_size = &task_container_size;
}

task_self_destroying::task_self_destroying(promise_self_destroying& promise)
    : m_promise(&promise)
{

}

task_self_destroying::~task_self_destroying()
{

}

task_self_destroying::task_self_destroying(task_self_destroying&& other)
    : m_promise(other.m_promise)
{

}

auto task_self_destroying::operator=(task_self_destroying&& other) -> task_self_destroying&
{
    if (std::addressof(other) != this)
    {
        m_promise = other.m_promise;
    }

    return *this;
}

} // namespace coro::detail

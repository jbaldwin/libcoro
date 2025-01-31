#include "coro/detail/task_self_deleting.hpp"

#include <utility>

namespace coro::detail
{

promise_self_deleting::promise_self_deleting()
{
    (void)m_task_container_size; // make codacy happy
}

promise_self_deleting::~promise_self_deleting()
{
}

promise_self_deleting::promise_self_deleting(promise_self_deleting&& other)
    : m_task_container_size(std::exchange(other.m_task_container_size, nullptr))
{
}

auto promise_self_deleting::operator=(promise_self_deleting&& other) -> promise_self_deleting&
{
    if (std::addressof(other) != nullptr)
    {
        m_task_container_size = std::exchange(other.m_task_container_size, nullptr);
    }

    return *this;
}

auto promise_self_deleting::get_return_object() -> task_self_deleting
{
    return task_self_deleting{*this};
}

auto promise_self_deleting::initial_suspend() -> std::suspend_always
{
    return std::suspend_always{};
}

auto promise_self_deleting::final_suspend() noexcept -> std::suspend_never
{
    // Notify the task_container<executor_t> that this coroutine has completed.
    if (m_task_container_size != nullptr)
    {
        m_task_container_size->fetch_sub(1);
    }

    // By not suspending this lets the coroutine destroy itself.
    return std::suspend_never{};
}

auto promise_self_deleting::return_void() noexcept -> void
{
    // no-op
}

auto promise_self_deleting::unhandled_exception() -> void
{
    // The user cannot access the promise anyways, ignore the exception.
}

auto promise_self_deleting::task_container_size(std::atomic<std::size_t>& task_container_size) -> void
{
    m_task_container_size = &task_container_size;
}

task_self_deleting::task_self_deleting(promise_self_deleting& promise) : m_promise(&promise)
{
}

task_self_deleting::~task_self_deleting()
{
}

task_self_deleting::task_self_deleting(task_self_deleting&& other) : m_promise(other.m_promise)
{
}

auto task_self_deleting::operator=(task_self_deleting&& other) -> task_self_deleting&
{
    if (std::addressof(other) != this)
    {
        m_promise = other.m_promise;
    }

    return *this;
}

} // namespace coro::detail

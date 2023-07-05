#pragma once

#include "coro/task.hpp"

namespace coro
{
template<typename T = void>
struct task_completion_source
{
private:
    coro::task<> suspend() { co_await std::suspend_always(); }
    coro::task<> suspended{suspend()};
    T            value;

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = delete;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = delete;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value(const T& v)
    {
        value = v;
        std::atomic_thread_fence(std::memory_order_release);
        suspended.resume();
    }
    void set_value(T&& v)
    {
        value = std::move(v);
        std::atomic_thread_fence(std::memory_order_release);
        suspended.resume();
    }
    coro::task<T> task()
    {
        co_await suspended;
        co_return std::move(value);
    }
};

template<>
struct task_completion_source<void>
{
private:
    coro::task<> suspend() { co_await std::suspend_always(); }
    coro::task<> suspended{suspend()};

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = delete;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = delete;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value()
    {
        suspended.resume();
    }
    coro::task<> task()
    {
        co_await suspended;
        co_return;
    }
};

} // namespace coro

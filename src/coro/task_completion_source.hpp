#pragma once

#include "coro/task.hpp"

namespace coro
{
template<typename T = void>
struct task_completion_source
{
private:
    struct suspend
    {
        std::atomic<bool>                    ready{false};
        std::atomic<std::coroutine_handle<>> coroutine{nullptr};

        bool await_ready() noexcept { return ready.exchange(true); }
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            coroutine.store(handle);
            ready.store(false);
        }
        constexpr void await_resume() const noexcept {}
    };

    suspend awaitable;

    std::optional<T> value;

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = delete;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = delete;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value(const T& v)
    {
        if (value.has_value())
            return;
        value = v;
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !awaitable.ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = awaitable.coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    void set_value(T&& v)
    {
        if (value.has_value())
            return;
        value = std::move(v);
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !awaitable.ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = awaitable.coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    coro::task<T> task()
    {
        co_await awaitable;
        co_return std::move(value.value());
    }
};

template<>
struct task_completion_source<void>
{
private:
    struct suspend
    {
        std::atomic<bool>                    ready{false};
        std::atomic<std::coroutine_handle<>> coroutine{nullptr};

        bool await_ready() noexcept { return ready.exchange(true); }
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            coroutine.store(handle);
            ready.store(false);
        }
        constexpr void await_resume() const noexcept {}
    };

    suspend awaitable;

    std::atomic<bool> is_set{false};

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = delete;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = delete;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value()
    {
        for (bool expect{false}; !is_set.compare_exchange_strong(expect, true);)
            return;
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !awaitable.ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = awaitable.coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    coro::task<> task()
    {
        co_await awaitable;
        co_return;
    }
};

} // namespace coro

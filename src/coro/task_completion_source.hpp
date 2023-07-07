#pragma once

#include "coro/task.hpp"
#include <atomic>
#include <memory>

namespace coro
{
template<typename T = void>
struct task_completion_source
{
private:
    struct suspendable
    {
        explicit suspendable() {}
        suspendable(const suspendable&)              = delete;
        suspendable(suspendable&& other) noexcept    = delete;
        auto                                 operator=(const suspendable&) -> suspendable& = delete;
        auto                                 operator=(suspendable&& other) noexcept -> suspendable& = delete;
        std::atomic<bool>                    is_set{false};
        T                                    value;
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

    std::shared_ptr<suspendable> suspender{std::make_shared<suspendable>()};

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = delete;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = delete;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value(const T& v)
    {
        for (bool expect{false}; !suspender->is_set.compare_exchange_strong(expect, true);)
            return;
        suspender->value = v;
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !suspender->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = suspender->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    void set_value(T&& v)
    {
        for (bool expect{false}; !suspender->is_set.compare_exchange_strong(expect, true);)
            return;
        suspender->value = v;
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !suspender->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = suspender->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    [[nodiscard]] coro::task<T> token() const
    {
        auto  suspender_ptr = suspender;
        auto& suspend       = *suspender_ptr;
        co_await suspend;
        co_return std::move(suspend.value);
    }
};

template<>
struct task_completion_source<void>
{
private:
    struct suspendable
    {
        explicit suspendable() {}
        suspendable(const suspendable&)              = delete;
        suspendable(suspendable&& other) noexcept    = delete;
        auto                                 operator=(const suspendable&) -> suspendable& = delete;
        auto                                 operator=(suspendable&& other) noexcept -> suspendable& = delete;
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

    std::shared_ptr<suspendable> suspender{std::make_shared<suspendable>()};

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
        for (bool expect{false}; !suspender->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = suspender->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    [[nodiscard]] coro::task<> token() const
    {
        auto  suspender_ptr = suspender;
        auto& suspend       = *suspender_ptr;
        co_await suspend;
        co_return;
    }
};

} // namespace coro

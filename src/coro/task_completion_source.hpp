#pragma once

#include <atomic>
#include <memory>

namespace coro
{
template<typename T = void>
struct task_completion_source
{
private:
    struct state_type
    {
        explicit state_type() {}
        state_type(const state_type&)                = delete;
        state_type(state_type&& other) noexcept      = delete;
        auto                                 operator=(const state_type&) -> state_type& = delete;
        auto                                 operator=(state_type&& other) noexcept -> state_type& = delete;
        std::atomic<bool>                    is_set{false};
        std::atomic<bool>                    ready{false};
        std::shared_ptr<T>                   value;
        std::atomic<std::coroutine_handle<>> coroutine{nullptr};
    };
    struct awaitable
    {
        explicit awaitable(std::shared_ptr<state_type>& _state) : state(_state) {}
        awaitable(const awaitable&)           = default;
        awaitable(awaitable&& other) noexcept = default;
        auto                        operator=(const awaitable&) -> awaitable& = default;
        auto                        operator=(awaitable&& other) noexcept -> awaitable& = default;
        std::shared_ptr<state_type> state;

        bool await_ready() const noexcept { return state->ready.exchange(true); }
        void await_suspend(std::coroutine_handle<> handle) const noexcept
        {
            state->coroutine.store(handle);
            state->ready.store(false);
        }
        T await_resume() const noexcept { return *state->value; }
    };

    std::shared_ptr<state_type> state{std::make_shared<state_type>()};

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = default;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = default;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value(const T& v) const
    {
        for (bool expect{false}; !state->is_set.compare_exchange_strong(expect, true);)
            return;
        state->value = std::make_shared<T>(v);
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !state->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = state->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    void set_value(T&& v) const
    {
        for (bool expect{false}; !state->is_set.compare_exchange_strong(expect, true);)
            return;
        state->value = std::make_shared<T>(std::move(v));
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !state->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = state->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    [[nodiscard]] auto token() { return awaitable(state); }
};

template<>
struct task_completion_source<void>
{
private:
    struct state_type
    {
        explicit state_type() {}
        state_type(const state_type&)                = delete;
        state_type(state_type&& other) noexcept      = delete;
        auto                                 operator=(const state_type&) -> state_type& = delete;
        auto                                 operator=(state_type&& other) noexcept -> state_type& = delete;
        std::atomic<bool>                    is_set{false};
        std::atomic<bool>                    ready{false};
        std::atomic<std::coroutine_handle<>> coroutine{nullptr};
    };
    struct awaitable
    {
        explicit awaitable(std::shared_ptr<state_type>& _state) : state(_state) {}
        awaitable(const awaitable&)           = default;
        awaitable(awaitable&& other) noexcept = default;
        auto                        operator=(const awaitable&) -> awaitable& = default;
        auto                        operator=(awaitable&& other) noexcept -> awaitable& = default;
        std::shared_ptr<state_type> state;

        bool await_ready() const noexcept { return state->ready.exchange(true); }
        void await_suspend(std::coroutine_handle<> handle) const noexcept
        {
            state->coroutine.store(handle);
            state->ready.store(false);
        }
        constexpr void await_resume() const noexcept {}
    };

    std::shared_ptr<state_type> state{std::make_shared<state_type>()};

public:
    task_completion_source() {}
    task_completion_source(const task_completion_source&)           = default;
    task_completion_source(task_completion_source&& other) noexcept = default;
    auto operator=(const task_completion_source&) -> task_completion_source& = default;
    auto operator=(task_completion_source&& other) noexcept -> task_completion_source& = default;
    void set_value() const
    {
        for (bool expect{false}; !state->is_set.compare_exchange_strong(expect, true);)
            return;
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !state->ready.compare_exchange_weak(expect, true); expect = false) {}
        auto coroutine = state->coroutine.load();
        if (coroutine)
            coroutine.resume();
    }
    [[nodiscard]] auto token() { return awaitable(state); }
};

} // namespace coro

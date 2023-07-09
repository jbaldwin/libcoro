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
    struct awaiter
    {
        explicit awaiter() {}
        explicit awaiter(std::shared_ptr<state_type>& _state) : state(_state) {}
        awaiter(const awaiter&)           = default;
        awaiter(awaiter&& other) noexcept = default;
        auto                        operator=(const awaiter&) -> awaiter& = default;
        auto                        operator=(awaiter&& other) noexcept -> awaiter& = default;
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
    }
    void set_value(T&& v) const
    {
        for (bool expect{false}; !state->is_set.compare_exchange_strong(expect, true);)
            return;
        state->value = std::make_shared<T>(std::move(v));
        std::atomic_thread_fence(std::memory_order_release);
        for (bool expect{false}; !state->ready.compare_exchange_weak(expect, true); expect = false) {}
    }
    auto               handle() const { return state->coroutine.load(); }
    [[nodiscard]] auto token() { return awaiter(state); }
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
    struct awaiter
    {
        explicit awaiter(std::shared_ptr<state_type>& _state) : state(_state) {}
        awaiter(const awaiter&)           = default;
        awaiter(awaiter&& other) noexcept = default;
        auto                        operator=(const awaiter&) -> awaiter& = default;
        auto                        operator=(awaiter&& other) noexcept -> awaiter& = default;
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
    }
    auto               handle() const { return state->coroutine.load(); }
    [[nodiscard]] auto token() { return awaiter(state); }
};

} // namespace coro

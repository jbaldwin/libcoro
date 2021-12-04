#pragma once

#include "coro/concepts/promise.hpp"

#include <coroutine>
#include <exception>
#include <utility>

namespace coro
{
template<typename return_type = void>
class task;

namespace detail
{
struct promise_base
{
    friend struct final_awaitable;
    struct final_awaitable
    {
        auto await_ready() const noexcept -> bool { return false; }

        template<typename promise_type>
        auto await_suspend(std::coroutine_handle<promise_type> coroutine) noexcept -> std::coroutine_handle<>
        {
            // If there is a continuation call it, otherwise this is the end of the line.
            auto& promise = coroutine.promise();
            if (promise.m_continuation != nullptr)
            {
                return promise.m_continuation;
            }
            else
            {
                return std::noop_coroutine();
            }
        }

        auto await_resume() noexcept -> void
        {
            // no-op
        }
    };

    promise_base() noexcept = default;
    ~promise_base()         = default;

    auto initial_suspend() { return std::suspend_always{}; }

    auto final_suspend() noexcept(true) { return final_awaitable{}; }

    auto unhandled_exception() -> void { m_exception_ptr = std::current_exception(); }

    auto continuation(std::coroutine_handle<> continuation) noexcept -> void { m_continuation = continuation; }

protected:
    std::coroutine_handle<> m_continuation{nullptr};
    std::exception_ptr      m_exception_ptr{};
};

template<typename return_type>
struct promise final : public promise_base
{
    using task_type        = task<return_type>;
    using coroutine_handle = std::coroutine_handle<promise<return_type>>;

    promise() noexcept = default;
    ~promise()         = default;

    auto get_return_object() noexcept -> task_type;

    auto return_value(return_type value) -> void { m_return_value = std::move(value); }

    auto result() const& -> const return_type&
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }

        return m_return_value;
    }

    auto result() && -> return_type&&
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }

        return std::move(m_return_value);
    }

private:
    return_type m_return_value;
};

template<>
struct promise<void> : public promise_base
{
    using task_type        = task<void>;
    using coroutine_handle = std::coroutine_handle<promise<void>>;

    promise() noexcept = default;
    ~promise()         = default;

    auto get_return_object() noexcept -> task_type;

    auto return_void() noexcept -> void {}

    auto result() -> void
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }
    }
};

} // namespace detail

template<typename return_type>
class [[nodiscard]] task
{
public:
    using task_type        = task<return_type>;
    using promise_type     = detail::promise<return_type>;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct awaitable_base
    {
        awaitable_base(coroutine_handle coroutine) noexcept : m_coroutine(coroutine) {}

        auto await_ready() const noexcept -> bool { return !m_coroutine || m_coroutine.done(); }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            m_coroutine.promise().continuation(awaiting_coroutine);
            return m_coroutine;
        }

        std::coroutine_handle<promise_type> m_coroutine{nullptr};
    };

    task() noexcept : m_coroutine(nullptr) {}

    explicit task(coroutine_handle handle) : m_coroutine(handle) {}
    task(const task&) = delete;
    task(task && other) noexcept : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

    ~task()
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
        }
    }

    auto operator=(const task&)->task& = delete;

    auto operator=(task&& other) noexcept->task&
    {
        if (std::addressof(other) != this)
        {
            if (m_coroutine != nullptr)
            {
                m_coroutine.destroy();
            }

            m_coroutine = std::exchange(other.m_coroutine, nullptr);
        }

        return *this;
    }

    /**
     * @return True if the task is in its final suspend or if the task has been destroyed.
     */
    auto is_ready() const noexcept->bool { return m_coroutine == nullptr || m_coroutine.done(); }

    auto resume()->bool
    {
        if (!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto destroy()->bool
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    auto operator co_await() const& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto)
            {
                if constexpr (std::is_same_v<void, return_type>)
                {
                    // Propagate uncaught exceptions.
                    this->m_coroutine.promise().result();
                    return;
                }
                else
                {
                    return this->m_coroutine.promise().result();
                }
            }
        };

        return awaitable{m_coroutine};
    }

    auto operator co_await() const&& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto)
            {
                if constexpr (std::is_same_v<void, return_type>)
                {
                    // Propagate uncaught exceptions.
                    this->m_coroutine.promise().result();
                    return;
                }
                else
                {
                    return std::move(this->m_coroutine.promise()).result();
                }
            }
        };

        return awaitable{m_coroutine};
    }

    auto promise()&->promise_type& { return m_coroutine.promise(); }

    auto promise() const&->const promise_type& { return m_coroutine.promise(); }
    auto promise()&&->promise_type&& { return std::move(m_coroutine.promise()); }

    auto handle()->coroutine_handle { return m_coroutine; }

private:
    coroutine_handle m_coroutine{nullptr};
};

namespace detail
{
template<typename return_type>
inline auto promise<return_type>::get_return_object() noexcept -> task<return_type>
{
    return task<return_type>{coroutine_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> task<>
{
    return task<>{coroutine_handle::from_promise(*this)};
}

} // namespace detail

} // namespace coro

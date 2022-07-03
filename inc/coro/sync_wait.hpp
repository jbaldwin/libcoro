#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/when_all.hpp"

#include <condition_variable>
#include <mutex>

namespace coro
{
namespace detail
{
class sync_wait_event
{
public:
    sync_wait_event(bool initially_set = false);
    sync_wait_event(const sync_wait_event&)                    = delete;
    sync_wait_event(sync_wait_event&&)                         = delete;
    auto operator=(const sync_wait_event&) -> sync_wait_event& = delete;
    auto operator=(sync_wait_event&&) -> sync_wait_event&      = delete;
    ~sync_wait_event()                                         = default;

    auto set() noexcept -> void;
    auto reset() noexcept -> void;
    auto wait() noexcept -> void;

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool                    m_set{false};
};

class sync_wait_task_promise_base
{
public:
    sync_wait_task_promise_base() noexcept = default;
    virtual ~sync_wait_task_promise_base() = default;

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }

    auto unhandled_exception() -> void { m_exception = std::current_exception(); }

protected:
    sync_wait_event*   m_event{nullptr};
    std::exception_ptr m_exception;
};

template<typename return_type>
class sync_wait_task_promise : public sync_wait_task_promise_base
{
public:
    using coroutine_type = std::coroutine_handle<sync_wait_task_promise<return_type>>;

    sync_wait_task_promise() noexcept  = default;
    ~sync_wait_task_promise() override = default;

    auto start(sync_wait_event& event)
    {
        m_event = &event;
        coroutine_type::from_promise(*this).resume();
    }

    auto get_return_object() noexcept { return coroutine_type::from_promise(*this); }

    auto yield_value(return_type&& value) noexcept
    {
        m_return_value = std::addressof(value);
        return final_suspend();
    }

    auto final_suspend() noexcept
    {
        struct completion_notifier
        {
            auto await_ready() const noexcept { return false; }
            auto await_suspend(coroutine_type coroutine) const noexcept { coroutine.promise().m_event->set(); }
            auto await_resume() noexcept {};
        };

        return completion_notifier{};
    }

    auto result() -> return_type&&
    {
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }

        return static_cast<return_type&&>(*m_return_value);
    }

private:
    std::remove_reference_t<return_type>* m_return_value;
};

template<>
class sync_wait_task_promise<void> : public sync_wait_task_promise_base
{
    using coroutine_type = std::coroutine_handle<sync_wait_task_promise<void>>;

public:
    sync_wait_task_promise() noexcept  = default;
    ~sync_wait_task_promise() override = default;

    auto start(sync_wait_event& event)
    {
        m_event = &event;
        coroutine_type::from_promise(*this).resume();
    }

    auto get_return_object() noexcept { return coroutine_type::from_promise(*this); }

    auto final_suspend() noexcept
    {
        struct completion_notifier
        {
            auto await_ready() const noexcept { return false; }
            auto await_suspend(coroutine_type coroutine) const noexcept { coroutine.promise().m_event->set(); }
            auto await_resume() noexcept {};
        };

        return completion_notifier{};
    }

    auto return_void() noexcept -> void {}

    auto result() -> void
    {
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }
    }
};

template<typename return_type>
class sync_wait_task
{
public:
    using promise_type   = sync_wait_task_promise<return_type>;
    using coroutine_type = std::coroutine_handle<promise_type>;

    sync_wait_task(coroutine_type coroutine) noexcept : m_coroutine(coroutine) {}

    sync_wait_task(const sync_wait_task&) = delete;
    sync_wait_task(sync_wait_task&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, coroutine_type{})) {}
    auto operator=(const sync_wait_task&) -> sync_wait_task& = delete;
    auto operator=(sync_wait_task&& other) -> sync_wait_task&
    {
        if (std::addressof(other) != this)
        {
            m_coroutine = std::exchange(other.m_coroutine, coroutine_type{});
        }

        return *this;
    }

    ~sync_wait_task()
    {
        if (m_coroutine)
        {
            m_coroutine.destroy();
        }
    }

    auto start(sync_wait_event& event) noexcept { m_coroutine.promise().start(event); }

    auto return_value() -> decltype(auto)
    {
        if constexpr (std::is_same_v<void, return_type>)
        {
            // Propagate exceptions.
            m_coroutine.promise().result();
            return;
        }
        else
        {
            return m_coroutine.promise().result();
        }
    }

private:
    coroutine_type m_coroutine;
};

template<
    concepts::awaitable awaitable_type,
    typename return_type = concepts::awaitable_traits<awaitable_type>::awaiter_return_type>
static auto make_sync_wait_task(awaitable_type&& a) -> sync_wait_task<return_type> __attribute__((used));

template<concepts::awaitable awaitable_type, typename return_type>
static auto make_sync_wait_task(awaitable_type&& a) -> sync_wait_task<return_type>
{
    if constexpr (std::is_void_v<return_type>)
    {
        co_await std::forward<awaitable_type>(a);
        co_return;
    }
    else
    {
        co_yield co_await std::forward<awaitable_type>(a);
    }
}

} // namespace detail

template<concepts::awaitable awaitable_type>
auto sync_wait(awaitable_type&& a) -> decltype(auto)
{
    detail::sync_wait_event e{};
    auto                    task = detail::make_sync_wait_task(std::forward<awaitable_type>(a));
    task.start(e);
    e.wait();

    return task.return_value();
}

} // namespace coro

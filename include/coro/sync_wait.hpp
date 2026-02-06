#pragma once

#include "coro/attribute.hpp"
#include "coro/concepts/awaitable.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <variant>

namespace coro
{
namespace detail
{

struct unset_return_value
{
    unset_return_value() {}
    unset_return_value(unset_return_value&&)      = delete;
    unset_return_value(const unset_return_value&) = delete;
    auto operator=(unset_return_value&&)          = delete;
    auto operator=(const unset_return_value&)     = delete;
};

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
    std::atomic<bool>       m_set{false};
};

class sync_wait_task_promise_base
{
public:
    sync_wait_task_promise_base() noexcept = default;

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }

protected:
    sync_wait_event*   m_event{nullptr};

    virtual ~sync_wait_task_promise_base() = default;
};

template<typename return_type>
class sync_wait_task_promise : public sync_wait_task_promise_base
{
public:
    using coroutine_type = std::coroutine_handle<sync_wait_task_promise<return_type>>;

    static constexpr bool return_type_is_reference = std::is_reference_v<return_type>;
    using stored_type                              = std::conditional_t<
                                     return_type_is_reference,
                                     std::remove_reference_t<return_type>*,
                                     std::remove_const_t<return_type>>;
    using variant_type = std::variant<unset_return_value, stored_type, std::exception_ptr>;

    sync_wait_task_promise() noexcept                                        = default;
    sync_wait_task_promise(const sync_wait_task_promise&)                    = delete;
    sync_wait_task_promise(sync_wait_task_promise&&)                         = delete;
    auto operator=(const sync_wait_task_promise&) -> sync_wait_task_promise& = delete;
    auto operator=(sync_wait_task_promise&&) -> sync_wait_task_promise&      = delete;
    ~sync_wait_task_promise() override                                       = default;

    auto start(sync_wait_event& event)
    {
        m_event = &event;
        coroutine_type::from_promise(*this).resume();
    }

    auto get_return_object() noexcept { return coroutine_type::from_promise(*this); }

    template<typename value_type>
        requires(return_type_is_reference and std::is_constructible_v<return_type, value_type &&>) or
                (not return_type_is_reference and std::is_constructible_v<stored_type, value_type &&>)
    auto return_value(value_type&& value) -> void
    {
        if constexpr (return_type_is_reference)
        {
            return_type ref = static_cast<value_type&&>(value);
            m_storage.template emplace<stored_type>(std::addressof(ref));
        }
        else
        {
            m_storage.template emplace<stored_type>(std::forward<value_type>(value));
        }
    }

    auto return_value(stored_type value) -> void
        requires(not return_type_is_reference)
    {
        if constexpr (std::is_move_constructible_v<stored_type>)
        {
            m_storage.template emplace<stored_type>(std::move(value));
        }
        else
        {
            m_storage.template emplace<stored_type>(value);
        }
    }

    auto unhandled_exception() noexcept -> void
    {
        m_storage.template emplace<std::exception_ptr>(std::current_exception());
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

    auto result() & -> decltype(auto)
    {
        if (std::holds_alternative<stored_type>(m_storage))
        {
            if constexpr (return_type_is_reference)
            {
                return static_cast<return_type>(*std::get<stored_type>(m_storage));
            }
            else
            {
                return static_cast<const return_type&>(std::get<stored_type>(m_storage));
            }
        }
        else if (std::holds_alternative<std::exception_ptr>(m_storage))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(m_storage));
        }
        else
        {
            throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
        }
    }

    auto result() const& -> decltype(auto)
    {
        if (std::holds_alternative<stored_type>(m_storage))
        {
            if constexpr (return_type_is_reference)
            {
                return static_cast<std::add_const_t<return_type>>(*std::get<stored_type>(m_storage));
            }
            else
            {
                return static_cast<const return_type&>(std::get<stored_type>(m_storage));
            }
        }
        else if (std::holds_alternative<std::exception_ptr>(m_storage))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(m_storage));
        }
        else
        {
            throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
        }
    }

    auto result() && -> decltype(auto)
    {
        if (std::holds_alternative<stored_type>(m_storage))
        {
            if constexpr (return_type_is_reference)
            {
                return static_cast<return_type>(*std::get<stored_type>(m_storage));
            }
            else if constexpr (std::is_constructible_v<return_type, stored_type>)
            {
                return static_cast<return_type&&>(std::get<stored_type>(m_storage));
            }
            else
            {
                return static_cast<const return_type&&>(std::get<stored_type>(m_storage));
            }
        }
        else if (std::holds_alternative<std::exception_ptr>(m_storage))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(m_storage));
        }
        else
        {
            throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
        }
    }

private:
    variant_type m_storage{};
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

    auto unhandled_exception() -> void { m_exception = std::current_exception(); }

    auto return_void() noexcept -> void {}

    auto result() -> void
    {
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }
    }

private:
    std::exception_ptr m_exception;
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

    auto promise() & -> promise_type& { return m_coroutine.promise(); }
    auto promise() const& -> const promise_type& { return m_coroutine.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_coroutine.promise()); }

private:
    coroutine_type m_coroutine;
};

template<
    concepts::awaitable awaitable_type,
    typename return_type = concepts::awaitable_traits<awaitable_type>::awaiter_return_type>
static auto make_sync_wait_task(awaitable_type&& a) -> sync_wait_task<return_type> __ATTRIBUTE__(used);

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
        co_return co_await std::forward<awaitable_type>(a);
    }
}

} // namespace detail

template<
    concepts::awaitable awaitable_type,
    typename return_type = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>
auto sync_wait(awaitable_type&& a) -> return_type
{
    detail::sync_wait_event e{};
    auto                    task = detail::make_sync_wait_task(std::forward<awaitable_type>(a));
    task.promise().start(e);
    e.wait();

    if constexpr (std::is_void_v<return_type>)
    {
        task.promise().result();
        return;
    }
    else if constexpr (std::is_reference_v<return_type>)
    {
        return task.promise().result();
    }
    else if constexpr (std::is_move_constructible_v<std::remove_const_t<return_type>>)
    {
        return std::move(task).promise().result();
    }
    else
    {
        return task.promise().result();
    }
}

} // namespace coro

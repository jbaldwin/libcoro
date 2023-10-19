#pragma once

#include <coroutine>
#include <exception>
#include <stdexcept>
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

    auto initial_suspend() noexcept { return std::suspend_always{}; }

    auto final_suspend() noexcept { return final_awaitable{}; }

    auto continuation(std::coroutine_handle<> continuation) noexcept -> void { m_continuation = continuation; }

protected:
    std::coroutine_handle<> m_continuation{nullptr};
};

template<typename return_type>
struct promise final : public promise_base
{
    enum class task_state : unsigned char
    {
        empty,
        value,
        exception,
    };
    using task_type                                = task<return_type>;
    using coroutine_handle                         = std::coroutine_handle<promise<return_type>>;
    static constexpr bool return_type_is_reference = std::is_reference_v<return_type>;
    using stored_type                              = std::conditional_t<
                                     return_type_is_reference,
                                     std::remove_reference_t<return_type>*,
                                     std::remove_const_t<return_type>>;
    static constexpr auto storage_align = alignof(std::exception_ptr) > alignof(stored_type)
                                              ? alignof(std::exception_ptr)
                                              : alignof(stored_type);
    static constexpr auto storage_size  = sizeof(std::exception_ptr) > sizeof(stored_type) ? sizeof(std::exception_ptr)
                                                                                           : sizeof(stored_type);

    promise() noexcept : m_state(task_state::empty) {}
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()
    {
        if (m_state == task_state::value)
        {
            if constexpr (not return_type_is_reference)
            {
                access_value().~stored_type();
            }
        }
        else if (m_state == task_state::exception)
        {
            access_exception().~exception_ptr();
        }
    }

    auto get_return_object() noexcept -> task_type;

    template<typename Value>
        requires(return_type_is_reference and std::is_constructible_v<return_type, Value &&>) or
                (not return_type_is_reference and std::is_constructible_v<stored_type, Value &&>)
    auto return_value(Value&& value) -> void
    {
        if constexpr (return_type_is_reference)
        {
            return_type ref = static_cast<Value&&>(value);
            new (m_storage) stored_type(std::addressof(ref));
        }
        else
        {
            new (m_storage) stored_type(static_cast<Value&&>(value));
        }
        m_state = task_state::value;
    }

    auto return_value(stored_type value) -> void
        requires(not return_type_is_reference)
    {
        if constexpr (std::is_move_constructible_v<stored_type>)
        {
            new (m_storage) stored_type(std::move(value));
        }
        else
        {
            new (m_storage) stored_type(value);
        }
        m_state = task_state::value;
    }

    auto unhandled_exception() noexcept -> void
    {
        new (m_storage) std::exception_ptr(std::current_exception());
        m_state = task_state::exception;
    }

    auto result() & -> decltype(auto)
    {
        switch (m_state)
        {
            case task_state::value:
                if constexpr (return_type_is_reference)
                {
                    return static_cast<return_type>(*access_value());
                }
                else
                {
                    return static_cast<const stored_type&>(access_value());
                }
                break;
            case task_state::exception:
                std::rethrow_exception(access_exception());
                break;
            default:
                throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
                break;
        }
    }

    auto result() const& -> decltype(auto)
    {
        switch (m_state)
        {
            case task_state::value:
                if constexpr (return_type_is_reference)
                {
                    return static_cast<std::add_const_t<return_type>>(*access_value());
                }
                else
                {
                    return static_cast<const stored_type&>(access_value());
                }
                break;
            case task_state::exception:
                std::rethrow_exception(access_exception());
                break;
            default:
                throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
                break;
        }
    }

    auto result() && -> decltype(auto)
    {
        switch (m_state)
        {
            case task_state::value:
                if constexpr (return_type_is_reference)
                {
                    return static_cast<return_type>(*access_value());
                }
                else if constexpr (std::is_move_constructible_v<stored_type>)
                {
                    return static_cast<stored_type&&>(access_value());
                }
                else
                {
                    return static_cast<const stored_type&&>(access_value());
                }
                break;
            case task_state::exception:
                std::rethrow_exception(access_exception());
                break;
            default:
                throw std::runtime_error{"The return value was never set, did you execute the coroutine?"};
                break;
        }
    }

private:
    alignas(storage_align) unsigned char m_storage[storage_size];
    task_state m_state;

    const stored_type& access_value() const noexcept
    {
        return *std::launder(reinterpret_cast<const stored_type*>(m_storage));
    }

    stored_type& access_value() noexcept { return *std::launder(reinterpret_cast<stored_type*>(m_storage)); }

    const std::exception_ptr& access_exception() const noexcept
    {
        return *std::launder(reinterpret_cast<const std::exception_ptr*>(m_storage));
    }

    std::exception_ptr& access_exception() noexcept
    {
        return *std::launder(reinterpret_cast<std::exception_ptr*>(m_storage));
    }
};

template<>
struct promise<void> : public promise_base
{
    using task_type        = task<void>;
    using coroutine_handle = std::coroutine_handle<promise<void>>;

    promise() noexcept                  = default;
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()                          = default;

    auto get_return_object() noexcept -> task_type;

    auto return_void() noexcept -> void {}

    auto unhandled_exception() noexcept -> void { m_exception_ptr = std::current_exception(); }

    auto result() -> void
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }
    }

private:
    std::exception_ptr m_exception_ptr{nullptr};
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
    task(task&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

    ~task()
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
        }
    }

    auto operator=(const task&) -> task& = delete;

    auto operator=(task&& other) noexcept -> task&
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
    auto is_ready() const noexcept -> bool { return m_coroutine == nullptr || m_coroutine.done(); }

    auto resume() -> bool
    {
        if (!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto destroy() -> bool
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
            auto await_resume() -> decltype(auto) { return this->m_coroutine.promise().result(); }
        };

        return awaitable{m_coroutine};
    }

    auto operator co_await() const&& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto) { return std::move(this->m_coroutine.promise()).result(); }
        };

        return awaitable{m_coroutine};
    }

    auto promise() & -> promise_type& { return m_coroutine.promise(); }
    auto promise() const& -> const promise_type& { return m_coroutine.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_coroutine.promise()); }

    auto handle() -> coroutine_handle { return m_coroutine; }

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

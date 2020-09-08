#pragma once

#include <coroutine>
#include <optional>

namespace coro
{

template<
    typename return_type,
    typename initial_suspend_type = std::suspend_never,
    typename final_suspend_type = std::suspend_never>
class task
{
public:
    struct promise_type
    {
        /// The return value for this promise.
        return_type m_return_value;
        /// If an exception was thrown it will be stored here and re-thrown on accessing the return value.
        std::optional<std::exception_ptr> m_e;

        using coro_handle = std::coroutine_handle<promise_type>;

        auto get_return_object() -> task<return_type, initial_suspend_type, final_suspend_type>
        {
            return coro_handle::from_promise(*this);
        }

        auto initial_suspend()
        {
            return initial_suspend_type();
        }

        auto final_suspend()
        {
            return final_suspend_type();
        }

        auto return_void() -> void
        {
            // no-op
        }

        auto unhandled_exception() -> void
        {
            m_e = std::current_exception();
        }

        auto return_value(return_type value) -> void
        {
            m_return_value = std::move(value);
        }

        auto return_value() const & -> const return_type&
        {
            if(m_e.has_value())
            {
                std::rethrow_exception(m_e.value());
            }

            return m_return_value;
        }

        auto return_value() && -> return_type&&
        {
            if(m_e.has_value())
            {
                std::rethrow_exception(m_e.value());
            }

            return std::move(m_return_value);
        }

        auto yield_value(return_type value)
        {
            m_return_value = std::move(value);
            return std::suspend_always();
        }
    };

    using coro_handle = std::coroutine_handle<promise_type>;

    task(coro_handle handle) : m_handle(std::move(handle))
    {

    }
    task(const task&) = delete;
    task(task&&) = delete;
    auto operator=(const task&) -> task& = delete;
    auto operator=(task&&) -> task& = delete;

    auto is_done() const noexcept -> bool
    {
        return m_handle.done();
    }

    auto resume() -> bool
    {
        if(!m_handle.done())
        {
            m_handle.resume();
        }
        return !m_handle.done();
    }

    auto return_value() const & -> const return_type&
    {
        return m_handle.promise().return_value();
    }

    auto return_value() && -> return_type&&
    {
        return std::move(m_handle.promise()).return_value();
    }

private:
    coro_handle m_handle;
};

} // namespace coro

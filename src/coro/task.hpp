#pragma once

#include <atomic>
#include <coroutine>
#include <optional>

namespace coro
{

template<
    typename return_type = void,
    typename initial_suspend_type = std::suspend_always>
class task;

namespace detail
{

template<
    typename initial_suspend_type>
struct promise_base
{
    promise_base() noexcept = default;
    ~promise_base() = default;

    auto initial_suspend()
    {
        return initial_suspend_type();
    }

    auto final_suspend()
    {
        return std::suspend_always();
    }

    auto unhandled_exception() -> void
    {
        m_exception_ptr = std::current_exception();
    }

protected:
    std::optional<std::exception_ptr> m_exception_ptr;
};

template<
    typename return_type,
    typename initial_suspend_type>
struct promise : public promise_base<initial_suspend_type>
{
    using task_type = task<return_type, initial_suspend_type>;
    using coro_handle = std::coroutine_handle<promise<return_type, initial_suspend_type>>;

    promise() noexcept = default;
    ~promise() = default;

    auto get_return_object() -> task_type;

    auto return_value(return_type result) -> void
    {
        m_result = std::move(result);
    }

    auto result() const & -> const return_type&
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }

        return m_result;
    }

    auto result() && -> return_type&&
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }

        return std::move(m_result);
    }

private:
    return_type m_result;
};

template<
    typename initial_suspend_type>
struct promise<void, initial_suspend_type> : public promise_base<initial_suspend_type>
{
    using task_type = task<void, initial_suspend_type>;
    using coro_handle = std::coroutine_handle<promise<void, initial_suspend_type>>;

    promise() noexcept = default;
    ~promise() = default;

    auto get_return_object() -> task_type;

    auto return_void() -> void { }

    auto result() const -> void
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }
    }
};

} // namespace detail

template<
    typename return_type,
    typename initial_suspend_type>
class task
{
public:
    using task_type = task<return_type, initial_suspend_type>;
    using promise_type = detail::promise<return_type, initial_suspend_type>;
    using coro_handle = std::coroutine_handle<promise_type>;

    task() noexcept
        : m_handle(nullptr)
    {

    }

    task(coro_handle handle)
        : m_handle(handle)
    {

    }
    task(const task&) = delete;
    task(task&& other) noexcept
        : m_handle(other.m_handle)
    {
        other.m_handle = nullptr;
    }

    auto operator=(const task&) -> task& = delete;
    auto operator=(task&& other) noexcept -> task&
    {
        if(std::addressof(other) != this)
        {
            if(m_handle)
            {
                m_handle.destroy();
            }

            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
    }

    /**
     * @return True if the task is in its final suspend or if the task has been destroyed.
     */
    auto is_ready() const noexcept -> bool
    {
        return m_handle == nullptr || m_handle.done();
    }

    auto resume() -> bool
    {
        if(!m_handle.done())
        {
            m_handle.resume();
        }
        return !m_handle.done();
    }

    auto destroy() -> bool
    {
        if(m_handle != nullptr)
        {
            m_handle.destroy();
            m_handle = nullptr;
            return true;
        }

        return false;
    }

    struct awaiter
    {
        awaiter(const task_type& t) noexcept
            : m_task(t)
        {

        }

        auto await_ready() const noexcept -> bool
        {
            return m_task.is_ready();
        }

        auto await_suspend(std::coroutine_handle<>) noexcept -> void
        {
            // no-op, the handle passed in is the same as m_task.promise()
        }

        auto await_resume() noexcept -> return_type
        {
            return m_task.promise().result();
        }

        const task_type& m_task;
    };

    auto operator co_await() const noexcept -> awaiter
    {
        return awaiter(*this);
    }

    auto promise() const & -> const promise_type& { return m_handle.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_handle.promise()); }

private:
    coro_handle m_handle{nullptr};
};

namespace detail
{

template<
    typename return_type,
    typename initial_suspend_type>
auto promise<return_type, initial_suspend_type>::get_return_object()
    -> task_type
{
    return coro_handle::from_promise(*this);
}

template<
    typename initial_suspend_type>
auto promise<void,initial_suspend_type>::get_return_object()
    -> task_type
{
    return coro_handle::from_promise(*this);
}

} // namespace detail


} // namespace coro

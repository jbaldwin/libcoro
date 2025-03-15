#include "coro/condition_variable.hpp"

// this is necessary for std::lock_guard
#include <cassert>
#include <mutex>

namespace coro
{

#ifdef LIBCORO_FEATURE_NETWORKING

auto detail::strategy_based_on_io_scheduler::wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration)
    -> task<std::cv_status>
{
    assert(!m_scheduler.expired());

    auto mtx = lock.mutex();
    lock.unlock();

    if (auto sched = m_scheduler.lock())
    {
        auto result = co_await sched->schedule(wait_task(), duration);

        auto ulock = co_await mtx->lock();
        lock       = std::move(ulock);
        co_return result.has_value() ? std::cv_status::no_timeout : std::cv_status::timeout;
    }

    co_return std::cv_status::timeout;
}

auto detail::strategy_based_on_io_scheduler::wait_task() -> task<bool>
{
    co_await wait_for_notify();
    co_return true;
}

void detail::strategy_based_on_io_scheduler::lock() noexcept
{
    while (true)
    {
        void* unlocked{};
        if (m_lock.compare_exchange_weak(unlocked, this, std::memory_order::release, std::memory_order::acquire))
        {
            break;
        }
    }
}

void detail::strategy_based_on_io_scheduler::unlock() noexcept
{
    m_lock.store(nullptr, std::memory_order::release);
}

void detail::strategy_based_on_io_scheduler::insert_waiter(wait_operation* waiter) noexcept
{
    while (true)
    {
        wait_operation* current = m_internal_waiters.load(std::memory_order::acquire);
        waiter->m_next.store(current, std::memory_order::release);

        if (!m_internal_waiters.compare_exchange_weak(
                current, waiter, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        break;
    }
}

bool detail::strategy_based_on_io_scheduler::extract_waiter(wait_operation* waiter) noexcept
{
    std::lock_guard<detail::strategy_based_on_io_scheduler> guard{*this};
    bool                                                    result{};

    while (true)
    {
        wait_operation* current = m_internal_waiters.load(std::memory_order::acquire);

        if (!current)
        {
            break;
        }

        wait_operation* next = current->m_next.load(std::memory_order::acquire);

        if (current == waiter)
        {
            if (!m_internal_waiters.compare_exchange_weak(
                    current, next, std::memory_order::release, std::memory_order::acquire))
            {
                continue;
            }
        }

        while (next && next != waiter)
        {
            current = next;
            next    = current->m_next.load(std::memory_order::acquire);
        }

        if (!next)
        {
            break;
        }

        wait_operation* new_next = waiter->m_next.load(std::memory_order::acquire);

        if (!current->m_next.compare_exchange_strong(
                next, new_next, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        waiter->m_next.store(nullptr, std::memory_order::release);
        result = true;
        break;
    }

    return result;
}

detail::strategy_based_on_io_scheduler::strategy_based_on_io_scheduler(std::shared_ptr<io_scheduler> io_scheduler)
    : m_scheduler(io_scheduler)
{
    assert(io_scheduler);
}

void detail::strategy_based_on_io_scheduler::notify_one() noexcept
{
    assert(!m_scheduler.expired());

    if (auto waiter_guard = extract_one())
    {
        if (auto sched = m_scheduler.lock())
        {
            sched->resume(waiter_guard.value()->m_awaiting_coroutine);
        }
    }
}

void detail::strategy_based_on_io_scheduler::notify_all() noexcept
{
    assert(!m_scheduler.expired());

    if (auto waiter_guard = extract_all())
    {
        if (auto sched = m_scheduler.lock())
        {
            auto* waiter = waiter_guard.value();
            do
            {
                auto* next = waiter->m_next.load(std::memory_order::acquire);
                sched->resume(waiter->m_awaiting_coroutine);
                waiter = next;
            } while (waiter);
        }
    }
}

detail::strategy_based_on_io_scheduler::wait_operation_guard detail::strategy_based_on_io_scheduler::extract_all()
{
    wait_operation_guard result{this};

    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        if (!m_internal_waiters.compare_exchange_weak(
                current, nullptr, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        result.set_value(current);
        break;
    }

    return result;
}

detail::strategy_based_on_io_scheduler::wait_operation_guard detail::strategy_based_on_io_scheduler::extract_one()
{
    wait_operation_guard result{this};

    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        auto* next = current->m_next.load(std::memory_order::acquire);
        if (!m_internal_waiters.compare_exchange_weak(
                current, next, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        current->m_next.store(nullptr, std::memory_order::release);
        result.set_value(current);
        break;
    }

    return result;
}

detail::strategy_based_on_io_scheduler::wait_operation::wait_operation(detail::strategy_based_on_io_scheduler& strategy)
    : m_strategy(strategy)
{
}

detail::strategy_based_on_io_scheduler::wait_operation::~wait_operation()
{
    m_strategy.extract_waiter(this);
}

auto detail::strategy_based_on_io_scheduler::wait_operation::await_suspend(
    std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_strategy.insert_waiter(this);
    return true;
}

void detail::strategy_based_on_io_scheduler::wait_operation::await_resume() noexcept
{
}

detail::strategy_based_on_io_scheduler::wait_operation_guard::wait_operation_guard(
    detail::strategy_based_on_io_scheduler* cv) noexcept
    : m_cv(cv)
{
    m_cv->lock();
}

detail::strategy_based_on_io_scheduler::wait_operation_guard::~wait_operation_guard()
{
    m_cv->unlock();
}

detail::strategy_based_on_io_scheduler::wait_operation_guard::operator bool() const noexcept
{
    return m_value;
}

void detail::strategy_based_on_io_scheduler::wait_operation_guard::set_value(wait_operation* value) noexcept
{
    m_value = value;
}

detail::strategy_based_on_io_scheduler::wait_operation*
    detail::strategy_based_on_io_scheduler::wait_operation_guard::value() const noexcept
{
    return m_value;
}

#endif

detail::strategy_base::wait_operation::wait_operation(detail::strategy_base& strategy) : m_strategy(strategy)
{
}

bool detail::strategy_base::wait_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
{
    m_awaiting_coroutine = awaiting_coroutine;
    while (true)
    {
        wait_operation* current = m_strategy.m_internal_waiters.load(std::memory_order::acquire);
        m_next.store(current, std::memory_order::release);

        if (!m_strategy.m_internal_waiters.compare_exchange_weak(
                current, this, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        break;
    }
    return true;
}

void detail::strategy_base::wait_operation::await_resume() noexcept
{
}

void detail::strategy_base::notify_one() noexcept
{
    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        auto* next = current->m_next.load(std::memory_order::acquire);
        if (!m_internal_waiters.compare_exchange_weak(
                current, next, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        current->m_awaiting_coroutine.resume();
        break;
    }
}

void detail::strategy_base::notify_all() noexcept
{
    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        if (!m_internal_waiters.compare_exchange_weak(
                current, nullptr, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        auto* next         = current->m_next.load(std::memory_order::acquire);
        auto* locked_value = get_locked_value();

        if (next == locked_value)
        {
            // another thread in notify_all() has already taken this waiter
            break;
        }

        if (!current->m_next.compare_exchange_weak(
                next, locked_value, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        current->m_awaiting_coroutine.resume();

        do
        {
            current = next;
            next    = current->m_next.load(std::memory_order::acquire);
            current->m_awaiting_coroutine.resume();
        } while (next);
    }
}

detail::strategy_base::wait_operation* detail::strategy_base::get_locked_value() noexcept
{
    return reinterpret_cast<detail::strategy_base::wait_operation*>(this);
}

auto detail::strategy_base::wait_for_notify() -> detail::strategy_base::wait_operation
{
    return wait_operation{*this};
}

} // namespace coro

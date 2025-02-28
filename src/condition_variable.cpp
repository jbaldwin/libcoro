#include "coro/condition_variable.hpp"

// this is necessary for std::lock_guard
#include <cassert>
#include <mutex>

namespace coro
{

condition_variable::condition_variable(std::shared_ptr<io_scheduler> scheduler) : m_scheduler(scheduler)
{
}

void condition_variable::notify_one() noexcept
{
    assert(m_scheduler);

    if (auto waiter_guard = extract_one())
    {
        m_scheduler->resume(waiter_guard.value()->m_awaiting_coroutine);
    }
}

void condition_variable::notify_all() noexcept
{
    assert(m_scheduler);

    if (auto waiter_guard = extract_all())
    {
        auto* waiter = waiter_guard.value();
        do
        {
            auto* next = waiter->m_next.load(std::memory_order::acquire);
            m_scheduler->resume(waiter->m_awaiting_coroutine);
            waiter = next;
        } while (waiter);
    }
}

auto condition_variable::wait(scoped_lock& lock) -> task<void>
{
    using namespace std::chrono_literals;

    auto mtx = lock.get_mutex();
    lock.unlock();

    co_await wait_for_notify();

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return;
}

std::shared_ptr<io_scheduler> condition_variable::scheduler() const noexcept
{
    return m_scheduler;
}

void condition_variable::set_scheduler(std::shared_ptr<io_scheduler> scheduler)
{
    m_scheduler = scheduler;
}

auto condition_variable::wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration)
    -> task<std::cv_status>
{
    assert(m_scheduler);

    auto mtx = lock.get_mutex();
    lock.unlock();

    auto result = co_await m_scheduler->schedule(wait_task(), duration);

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return result.has_value() ? std::cv_status::no_timeout : std::cv_status::timeout;
}

auto condition_variable::wait_task() -> task<bool>
{
    co_await wait_for_notify();
    co_return true;
}

void condition_variable::lock() noexcept
{
    while (true)
    {
        void* unlocked{};
        if (m_lock.compare_exchange_weak(unlocked, this, std::memory_order::acq_rel))
        {
            break;
        }
    }
}

void condition_variable::unlock() noexcept
{
    m_lock.store(nullptr, std::memory_order::release);
}

void condition_variable::insert_waiter(wait_operation* waiter) noexcept
{
    while (true)
    {
        wait_operation* current = m_internal_waiters.load(std::memory_order::acquire);
        waiter->m_next.store(current, std::memory_order::release);

        if (!m_internal_waiters.compare_exchange_weak(current, waiter, std::memory_order::acq_rel))
        {
            continue;
        }

        break;
    }
}

bool condition_variable::extract_waiter(wait_operation* waiter) noexcept
{
    std::lock_guard<condition_variable> guard{*this};
    bool                                result{};

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
            if (!m_internal_waiters.compare_exchange_weak(current, next, std::memory_order::acq_rel))
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

        if (!current->m_next.compare_exchange_strong(next, new_next, std::memory_order::acq_rel))
        {
            continue;
        }

        waiter->m_next.store(nullptr, std::memory_order::release);
        result = true;
        break;
    }

    return result;
}

condition_variable::wait_operation_guard condition_variable::extract_all()
{
    wait_operation_guard result{this};

    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        if (!m_internal_waiters.compare_exchange_weak(current, nullptr, std::memory_order::acq_rel))
        {
            continue;
        }

        result.set_value(current);
        break;
    }

    return result;
}

condition_variable::wait_operation_guard condition_variable::extract_one()
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
        if (!m_internal_waiters.compare_exchange_weak(current, next, std::memory_order::acq_rel))
        {
            continue;
        }

        current->m_next.store(nullptr, std::memory_order::release);
        result.set_value(current);
        break;
    }

    return result;
}

condition_variable::wait_operation::wait_operation(condition_variable& cv) : m_condition_variable(cv)
{
}

condition_variable::wait_operation::~wait_operation()
{
    m_condition_variable.extract_waiter(this);
}

auto condition_variable::wait_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_condition_variable.insert_waiter(this);
    return true;
}

void condition_variable::wait_operation::await_resume() noexcept
{
}

condition_variable::wait_operation_guard::wait_operation_guard(condition_variable* cv) noexcept : m_cv(cv)
{
    m_cv->lock();
}

condition_variable::wait_operation_guard::~wait_operation_guard()
{
    m_cv->unlock();
}

condition_variable::wait_operation_guard::operator bool() const noexcept
{
    return m_value;
}

void condition_variable::wait_operation_guard::set_value(wait_operation* value) noexcept
{
    m_value = value;
}

condition_variable::wait_operation* condition_variable::wait_operation_guard::value() const noexcept
{
    return m_value;
}

} // namespace coro

#include "coro/condition_variable.hpp"
#include "coro/when_any.hpp"

#include <cassert>
#include <sys/eventfd.h>

namespace coro
{

condition_variable::condition_variable(std::shared_ptr<io_scheduler> scheduler) : m_scheduler(scheduler)
{
}

void condition_variable::notify_one() noexcept
{
    assert(m_scheduler);

    if (auto waiter_guard = extract_one())
        m_scheduler->resume(waiter_guard.value->m_awaiting_coroutine);
}

void condition_variable::notify_all() noexcept
{
    assert(m_scheduler);

    if (auto waiter_guard = extract_all())
    {
        auto* waiter = waiter_guard.value;
        do
        {
            auto* next = waiter->m_next.load();
            m_scheduler->resume(waiter->m_awaiting_coroutine);
            waiter = next;
        } while (waiter);
    }
}

auto condition_variable::wait(scoped_lock& lock) -> task<void>
{
    using namespace std::chrono_literals;

    auto mtx = lock.mutex();
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
    auto mtx = lock.mutex();
    lock.unlock();

    auto result = co_await m_scheduler->schedule(wait_task(this), duration);

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return result.has_value() ? std::cv_status::no_timeout : std::cv_status::timeout;
}

auto condition_variable::wait_task(condition_variable* cv) -> task<bool>
{
    co_await cv->wait_for_notify();
    co_return true;
}

void condition_variable::lock(void* ptr)
{
    assert(ptr);
    void* unlocked{};

    while (true)
    {
        unlocked = nullptr;
        if (m_lock.compare_exchange_strong(unlocked, ptr))
            break;
    }
}

void condition_variable::unlock()
{
    m_lock.store(nullptr);
}

void condition_variable::insert_waiter(wait_operation* waiter)
{
    while (true)
    {
        wait_operation* current = m_internal_waiters.load();
        waiter->m_next.store(current);

        if (!m_internal_waiters.compare_exchange_weak(current, waiter))
            continue;

        break;
    }
}

bool condition_variable::extract_waiter(wait_operation* waiter)
{
    wait_operation_guard guard{this, waiter};
    bool                 result{};

    while (true)
    {
        wait_operation* current = m_internal_waiters.load();

        if (!current)
            break;

        wait_operation* next = current->m_next.load();

        if (current == waiter)
        {
            if (!m_internal_waiters.compare_exchange_weak(current, next))
                continue;
        }

        while (next && next != waiter)
        {
            current = next;
            next    = current->m_next.load();
        }

        if (!next)
            break;

        wait_operation* new_next = waiter->m_next.load();

        if (!current->m_next.compare_exchange_strong(next, new_next))
            continue;

        waiter->m_next.store(nullptr);
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
        auto* current = m_internal_waiters.load();
        if (!current)
            break;

        if (!m_internal_waiters.compare_exchange_weak(current, nullptr))
            continue;

        result.value = current;
        break;
    }

    return result;
}

condition_variable::wait_operation_guard condition_variable::extract_one()
{
    wait_operation_guard result{this};

    while (true)
    {
        auto* current = m_internal_waiters.load();
        if (!current)
            break;

        auto* next = current->m_next.load();
        if (!m_internal_waiters.compare_exchange_weak(current, next))
            continue;

        current->m_next.store(nullptr);
        result.value = current;
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

condition_variable::wait_operation_guard::wait_operation_guard(condition_variable* cv, wait_operation* value)
    : cv(cv),
      value(value)
{
    cv->lock(cv);
}

coro::condition_variable::wait_operation_guard::~wait_operation_guard()
{
    cv->unlock();
}

condition_variable::wait_operation_guard::operator bool() const
{
    return value;
}

} // namespace coro

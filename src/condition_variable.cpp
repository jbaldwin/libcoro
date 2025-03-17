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

void detail::strategy_based_on_io_scheduler::insert_waiter(wait_operation* waiter) noexcept
{
    auto* link = acquire_link();
    link->waiter.store(waiter, std::memory_order::release);
    waiter->m_link = link;

    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        link->next.store(current, std::memory_order::release);

        if (!m_internal_waiters.compare_exchange_weak(
                current, link, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        break;
    }
}

void detail::strategy_based_on_io_scheduler::extract_waiter(wait_operation* waiter) noexcept
{
    std::lock_guard<wait_operation_link> lk(*waiter->m_link);
    waiter->m_link->waiter.store(nullptr, std::memory_order::release);
}

void detail::strategy_based_on_io_scheduler::release_link(wait_operation_link* link)
{
    while (true)
    {
        auto* current = m_free_links.load(std::memory_order::acquire);
        link->next.store(current, std::memory_order::release);

        if (!m_free_links.compare_exchange_weak(current, link, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        break;
    }
}

void detail::strategy_based_on_io_scheduler::recycle_links(wait_operation_link* links)
{
    while (links)
    {
        auto* next = links->next.load(std::memory_order::acquire);
        release_link(links);
        links = next;
    }
}

detail::strategy_based_on_io_scheduler::wait_operation_link* detail::strategy_based_on_io_scheduler::acquire_link()
{
    while (true)
    {
        auto* current = m_free_links.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        auto* next = current->next.load(std::memory_order::acquire);
        if (!m_free_links.compare_exchange_weak(current, next, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        return current;
    }

    return new wait_operation_link;
}

detail::strategy_based_on_io_scheduler::strategy_based_on_io_scheduler(std::shared_ptr<io_scheduler> io_scheduler)
    : m_scheduler(io_scheduler)
{
    assert(io_scheduler);
}

detail::strategy_based_on_io_scheduler::~strategy_based_on_io_scheduler()
{
    auto* waiter_link = m_free_links.load(std::memory_order::acquire);
    while (waiter_link)
    {
        auto* next = waiter_link->next.load(std::memory_order::acquire);
        delete waiter_link;
        waiter_link = next;
    }
}

void detail::strategy_based_on_io_scheduler::notify_one() noexcept
{
    assert(!m_scheduler.expired());

    if (auto waiter_link = extract_one())
    {
        if (auto sched = m_scheduler.lock())
        {
            std::lock_guard<wait_operation_link> lk(*waiter_link);
            if (auto* waiter = waiter_link->waiter.load(std::memory_order::acquire))
            {
                sched->resume(waiter->m_awaiting_coroutine);
            }
        }
    }
}

void detail::strategy_based_on_io_scheduler::notify_all() noexcept
{
    assert(!m_scheduler.expired());

    if (auto waiter_links = extract_all())
    {
        if (auto sched = m_scheduler.lock())
        {
            auto* waiter_link = waiter_links.get();
            while (waiter_link)
            {
                {
                    std::lock_guard<wait_operation_link> lk(*waiter_link);
                    if (auto* waiter = waiter_link->waiter.load(std::memory_order::acquire))
                    {
                        sched->resume(waiter->m_awaiting_coroutine);
                    }
                }
                auto* next  = waiter_link->next.load(std::memory_order::acquire);
                waiter_link = next;
            }
        }
    }
}

detail::strategy_based_on_io_scheduler::wait_operation_links detail::strategy_based_on_io_scheduler::extract_one()
{
    while (true)
    {
        auto* current = m_internal_waiters.load(std::memory_order::acquire);
        if (!current)
        {
            break;
        }

        auto* next = current->next.load(std::memory_order::acquire);
        if (!m_internal_waiters.compare_exchange_weak(
                current, next, std::memory_order::release, std::memory_order::acquire))
        {
            continue;
        }

        if (current->waiter.load(std::memory_order::acquire))
        {
            current->next.store(nullptr, std::memory_order::release);
            return {current, [this](wait_operation_link* ptr) { recycle_links(ptr); }};
        }

        // skip expired waiters
        release_link(current);
    }

    return nullptr;
}

detail::strategy_based_on_io_scheduler::wait_operation_links detail::strategy_based_on_io_scheduler::extract_all()
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

        while (current)
        {
            if (current->waiter.load(std::memory_order::acquire))
            {
                return {current, [this](wait_operation_link* ptr) { recycle_links(ptr); }};
            }

            // skip expired waiters
            auto* next = current->next.load(std::memory_order::acquire);
            release_link(current);
            current = next;
        }
    }

    return nullptr;
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

void detail::strategy_based_on_io_scheduler::wait_operation_link::lock() noexcept
{
    while (true)
    {
        void* unlocked{};
        if (access.compare_exchange_weak(unlocked, this, std::memory_order::release, std::memory_order::acquire))
        {
            break;
        }
    }
}

void detail::strategy_based_on_io_scheduler::wait_operation_link::unlock() noexcept
{
    access.store(nullptr, std::memory_order::release);
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

#include "coro/semaphore.hpp"

namespace coro
{
semaphore::semaphore(std::ptrdiff_t least_max_value_and_starting_value)
    : semaphore(least_max_value_and_starting_value, least_max_value_and_starting_value)
{
}

semaphore::semaphore(std::ptrdiff_t least_max_value, std::ptrdiff_t starting_value)
    : m_least_max_value(least_max_value),
      m_counter(starting_value <= least_max_value ? starting_value : least_max_value)
{
}

semaphore::~semaphore()
{
    stop_notify_all();
}

semaphore::acquire_operation::acquire_operation(semaphore& s) : m_semaphore(s)
{
}

auto semaphore::acquire_operation::await_ready() const noexcept -> bool
{
    return m_semaphore.try_acquire();
}

auto semaphore::acquire_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    std::unique_lock lk{m_semaphore.m_waiter_mutex};
    if (m_semaphore.m_notify_all_set)
    {
        return false;
    }

    if (m_semaphore.try_acquire())
    {
        return false;
    }

    if (m_semaphore.m_acquire_waiters == nullptr)
    {
        m_semaphore.m_acquire_waiters = this;
    }
    else
    {
        // This is LIFO, but semaphores are not meant to be fair.

        // Set our next to the current head.
        m_next = m_semaphore.m_acquire_waiters;
        // Set the semaphore head to this.
        m_semaphore.m_acquire_waiters = this;
    }

    m_awaiting_coroutine = awaiting_coroutine;
    return true;
}

auto semaphore::acquire_operation::await_resume() const noexcept -> bool
{
    return !m_semaphore.m_notify_all_set;
}

auto semaphore::release() -> void
{
    // It seems like the atomic counter could be incremented, but then resuming a waiter could have
    // a race between a new acquirer grabbing the just incremented resource value from us.  So its
    // best to check if there are any waiters first, and transfer owernship of the resource thats
    // being released directly to the waiter to avoid this problem.

    std::unique_lock lk{m_waiter_mutex};
    if (m_acquire_waiters != nullptr)
    {
        acquire_operation* to_resume = m_acquire_waiters;
        m_acquire_waiters            = m_acquire_waiters->m_next;
        lk.unlock();

        // This will transfer ownership of the resource to the resumed waiter.
        to_resume->m_awaiting_coroutine.resume();
    }
    else
    {
        // Normally would be release but within a lock use releaxed.
        m_counter.fetch_add(1, std::memory_order::relaxed);
    }
}

auto semaphore::try_acquire() -> bool
{
    // Optimistically grab the resource.
    auto previous = m_counter.fetch_sub(1, std::memory_order::acq_rel);
    if (previous <= 0)
    {
        // If it wasn't available undo the acquisition.
        m_counter.fetch_add(1, std::memory_order::release);
        return false;
    }
    return true;
}

auto semaphore::stop_notify_all() noexcept -> void
{
    while (true)
    {
        std::unique_lock lk{m_waiter_mutex};
        if (m_acquire_waiters != nullptr)
        {
            acquire_operation* to_resume = m_acquire_waiters;
            m_acquire_waiters            = m_acquire_waiters->m_next;
            lk.unlock();

            to_resume->m_awaiting_coroutine.resume();
        }
        else
        {
            break;
        }
    }
}

} // namespace coro

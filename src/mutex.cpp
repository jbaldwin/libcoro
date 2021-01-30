#include "coro/mutex.hpp"

namespace coro
{
scoped_lock::~scoped_lock()
{
    if (m_mutex != nullptr)
    {
        m_mutex->unlock();
    }
}

auto scoped_lock::unlock() -> void
{
    if (m_mutex != nullptr)
    {
        m_mutex->unlock();
        m_mutex = nullptr;
    }
}

auto mutex::lock_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    std::scoped_lock lk{m_mutex.m_waiter_mutex};
    if (m_mutex.try_lock())
    {
        // If we just straight up acquire the lock, don't suspend.
        return false;
    }

    // The lock is currently held, so append ourself to the waiter list.
    if (m_mutex.m_tail_waiter == nullptr)
    {
        // If there are no current waiters this lock operation is the head and tail.
        m_mutex.m_head_waiter = this;
        m_mutex.m_tail_waiter = this;
    }
    else
    {
        // Update the current tail pointer to ourself.
        m_mutex.m_tail_waiter->m_next = this;
        // Update the tail pointer on the mutex to ourself.
        m_mutex.m_tail_waiter = this;
    }

    m_awaiting_coroutine = awaiting_coroutine;
    return true;
}

auto mutex::try_lock() -> bool
{
    bool expected = false;
    return m_state.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed);
}

auto mutex::unlock() -> void
{
    // Acquire the next waiter before releasing _or_ moving ownship of the lock.
    lock_operation* next{nullptr};
    {
        std::scoped_lock lk{m_waiter_mutex};
        if (m_head_waiter != nullptr)
        {
            next          = m_head_waiter;
            m_head_waiter = m_head_waiter->m_next;

            // Null out the tail waiter if this was the last waiter.
            if (m_head_waiter == nullptr)
            {
                m_tail_waiter = nullptr;
            }
        }
        else
        {
            // If there were no waiters, release the lock.  This is done under the waiter list being
            // locked so another thread doesn't add themselves to the waiter list before the lock
            // is actually released.
            m_state.exchange(false, std::memory_order::release);
        }
    }

    // If there were any waiters resume the next in line, this will pass ownership of the mutex to
    // that waiter, only the final waiter in the list actually unlocks the mutex.
    if (next != nullptr)
    {
        next->m_awaiting_coroutine.resume();
    }
}

} // namespace coro

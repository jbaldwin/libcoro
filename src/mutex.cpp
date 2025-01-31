#include "coro/mutex.hpp"

#include <iostream>

namespace coro
{
scoped_lock::~scoped_lock()
{
    unlock();
}

auto scoped_lock::unlock() -> void
{
    if (m_mutex != nullptr)
    {
        std::atomic_thread_fence(std::memory_order::release);
        m_mutex->unlock();
        // Only allow a scoped lock to unlock the mutex a single time.
        m_mutex = nullptr;
    }
}

auto mutex::lock_operation::await_ready() const noexcept -> bool
{
    if (m_mutex.try_lock())
    {
        // Since there is no mutex acquired, insert a memory fence to act like it.
        std::atomic_thread_fence(std::memory_order::acquire);
        return true;
    }
    return false;
}

auto mutex::lock_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    void* current        = m_mutex.m_state.load(std::memory_order::acquire);
    void* new_value;

    const void* unlocked_value = m_mutex.unlocked_value();
    do
    {
        if (current == unlocked_value)
        {
            // If the current value is 'unlocked' then attempt to lock it.
            new_value = nullptr;
        }
        else
        {
            // If the current value is a waiting lock operation, or nullptr, set our next to that
            // lock op and attempt to set ourself as the head of the waiter list.
            m_next    = static_cast<lock_operation*>(current);
            new_value = static_cast<void*>(this);
        }
    } while (!m_mutex.m_state.compare_exchange_weak(current, new_value, std::memory_order::acq_rel));

    // Don't suspend if the state went from unlocked -> locked with zero waiters.
    if (current == unlocked_value)
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        m_awaiting_coroutine = nullptr; // nothing to await later since this doesn't suspend
        return false;
    }

    return true;
}

auto mutex::try_lock() -> bool
{
    void* expected = const_cast<void*>(unlocked_value());
    return m_state.compare_exchange_strong(expected, nullptr, std::memory_order::acq_rel, std::memory_order::relaxed);
}

auto mutex::unlock() -> void
{
    if (m_internal_waiters == nullptr)
    {
        void* current = m_state.load(std::memory_order::relaxed);
        if (current == nullptr)
        {
            // If there are no internal waiters and there are no atomic waiters, attempt to set the
            // mutex as unlocked.
            if (m_state.compare_exchange_strong(
                    current,
                    const_cast<void*>(unlocked_value()),
                    std::memory_order::release,
                    std::memory_order::relaxed))
            {
                return; // The mutex is now unlocked with zero waiters.
            }
            // else we failed to unlock, someone added themself as a waiter.
        }

        // There are waiters on the atomic list, acquire them and update the state for all others.
        m_internal_waiters = static_cast<lock_operation*>(m_state.exchange(nullptr, std::memory_order::acq_rel));

        // Should internal waiters be reversed to allow for true FIFO, or should they be resumed
        // in this reverse order to maximum throuhgput?  If this list ever gets 'long' the reversal
        // will take some time, but it might guarantee better latency across waiters.  This LIFO
        // middle ground on the atomic waiters means the best throughput at the cost of the first
        // waiter possibly having added latency based on the queue length of waiters.  Either way
        // incurs a cost but this way for short lists will most likely be faster even though it
        // isn't completely fair.
    }

    // assert m_internal_waiters != nullptr

    lock_operation* to_resume = m_internal_waiters;
    m_internal_waiters        = m_internal_waiters->m_next;
    to_resume->m_awaiting_coroutine.resume();
}

} // namespace coro

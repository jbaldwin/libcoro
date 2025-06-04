#include "coro/detail/awaiter_list.hpp"
#include "coro/mutex.hpp"

namespace coro
{
namespace detail
{
auto lock_operation_base::await_ready() const noexcept -> bool
{
    return m_mutex.try_lock();
}

auto lock_operation_base::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    auto& state = m_mutex.m_state;
    void* current = state.load(std::memory_order::acquire);
    const void* unlocked_value = m_mutex.unlocked_value();
    do
    {
        // While trying to suspend the lock can become available, if so attempt to grab it and then don't suspend.
        // If the lock never becomes available then we place ourself at the head of the waiter list and suspend.

        if (current == unlocked_value)
        {
            // The lock has become available, try and lock.
            if (state.compare_exchange_weak(current, nullptr, std::memory_order::acq_rel, std::memory_order::acquire))
            {
                // We've acquired the lock, don't suspend.
                m_awaiting_coroutine = nullptr;
                return false;
            }
        }
        else // if (current == nullptr || current is of type lock_operation_base*)
        {
            // The lock is still owned, attempt to add ourself as a waiter.
            m_next = static_cast<lock_operation_base*>(current);
            if (state.compare_exchange_weak(current, static_cast<void*>(this), std::memory_order::acq_rel, std::memory_order::acquire))
            {
                // We've successfully added ourself to the waiter queue.
                return true;
            }
        }
    } while (true);
}

} // namespace detail

scoped_lock::~scoped_lock()
{
    unlock();
}

auto scoped_lock::unlock() -> void
{
    if (m_mutex != nullptr)
    {
        std::atomic_thread_fence(std::memory_order::acq_rel);
        m_mutex->unlock();
        m_mutex = nullptr;
    }
}

auto mutex::try_lock() -> bool
{
    void* expected = const_cast<void*>(unlocked_value());
    return m_state.compare_exchange_strong(expected, nullptr, std::memory_order::acq_rel, std::memory_order::relaxed);
}

auto mutex::unlock() -> void
{
    void* current = m_state.load(std::memory_order::acquire);
    do
    {
        // Sanity check that the mutex isn't already unlocked.
        if (current == const_cast<void*>(unlocked_value()))
        {
            throw std::runtime_error{"coro::mutex is already unlocked"};
        }

        // There are no current waiters, attempt to set the mutex as unlocked.
        if (current == nullptr)
        {
            if (m_state.compare_exchange_weak(
                current,
                const_cast<void*>(unlocked_value()),
                std::memory_order::acq_rel,
                std::memory_order::acquire))
            {
                // We've successfully unlocked the mutex, return since there are no current waiters.
                std::atomic_thread_fence(std::memory_order::acq_rel);
                return;
            }
            else
            {
                // This means someone has added themselves as a waiter, we need to try again with our updated current state.
                // assert(m_state now holds a lock_operation_base*)
                continue;
            }
        }
        else
        {
            // There are waiters, lets wake the first one up. This will set the state to the next waiter, or nullptr (no waiters but locked).
            std::atomic<detail::lock_operation_base*>* casted = reinterpret_cast<std::atomic<detail::lock_operation_base*>*>(&m_state);
            auto* waiter = detail::awaiter_list_pop<detail::lock_operation_base>(*casted);
            // assert waiter != nullptr, nobody else should be unlocking this mutex.
            // Directly transfer control to the waiter, they are now responsible for unlocking the mutex.
            std::atomic_thread_fence(std::memory_order::acq_rel);
            waiter->m_awaiting_coroutine.resume();
            return;
        }
    } while (true);
}

} // namespace coro

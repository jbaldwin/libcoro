#include "coro/event.hpp"
#include "coro/thread_pool.hpp"

namespace coro
{
event::event(bool initially_set) noexcept : m_state((initially_set) ? static_cast<void*>(this) : nullptr)
{
}

auto event::set(resume_order_policy policy) noexcept -> void
{
    // Exchange the state to this, if the state was previously not this, then traverse the list
    // of awaiters and resume their coroutines.
    void* old_value = m_state.exchange(this, std::memory_order::acq_rel);
    if (old_value != this)
    {
        // If FIFO has been requsted then reverse the order upon resuming.
        if (policy == resume_order_policy::fifo)
        {
            old_value = reverse(static_cast<awaiter*>(old_value));
        }
        // else lifo nothing to do

        auto* waiters = static_cast<awaiter*>(old_value);
        while (waiters != nullptr)
        {
            auto* next = waiters->m_next;
            waiters->m_awaiting_coroutine.resume();
            waiters = next;
        }
    }
}

auto event::reverse(awaiter* curr) -> awaiter*
{
    if (curr == nullptr || curr->m_next == nullptr)
    {
        return curr;
    }

    awaiter* prev = nullptr;
    awaiter* next = nullptr;
    while (curr != nullptr)
    {
        next         = curr->m_next;
        curr->m_next = prev;
        prev         = curr;
        curr         = next;
    }

    return prev;
}

auto event::awaiter::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    const void* const set_state = &m_event;

    m_awaiting_coroutine = awaiting_coroutine;

    // This value will update if other threads write to it via acquire.
    void* old_value = m_event.m_state.load(std::memory_order::acquire);
    do
    {
        // Resume immediately if already in the set state.
        if (old_value == set_state)
        {
            return false;
        }

        m_next = static_cast<awaiter*>(old_value);
    } while (!m_event.m_state.compare_exchange_weak(
        old_value, this, std::memory_order::release, std::memory_order::acquire));

    return true;
}

auto event::reset() noexcept -> void
{
    void* old_value = this;
    m_state.compare_exchange_strong(old_value, nullptr, std::memory_order::acquire);
}

} // namespace coro

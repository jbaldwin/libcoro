#pragma once

#include <atomic>
#include <coroutine>
#include <optional>

namespace coro
{
class event
{
  public:
    event(bool initially_set = false) noexcept : m_state((initially_set) ? static_cast<void*>(this) : nullptr) {}
    virtual ~event() = default;

    event(const event&) = delete;
    event(event&&)      = delete;
    auto operator=(const event&) -> event& = delete;
    auto operator=(event &&) -> event& = delete;

    bool is_set() const noexcept { return m_state.load(std::memory_order_acquire) == this; }

    auto set() noexcept -> void
    {
        void* old_value = m_state.exchange(this, std::memory_order_acq_rel);
        if (old_value != this)
        {
            auto* waiters = static_cast<awaiter*>(old_value);
            while (waiters != nullptr)
            {
                auto* next = waiters->m_next;
                waiters->m_awaiting_coroutine.resume();
                waiters = next;
            }
        }
    }

    struct awaiter
    {
        awaiter(const event& event) noexcept : m_event(event) {}

        auto await_ready() const noexcept -> bool { return m_event.is_set(); }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            const void* const set_state = &m_event;

            m_awaiting_coroutine = awaiting_coroutine;

            // This value will update if other threads write to it via acquire.
            void* old_value = m_event.m_state.load(std::memory_order_acquire);
            do
            {
                // Resume immediately if already in the set state.
                if (old_value == set_state)
                {
                    return false;
                }

                m_next = static_cast<awaiter*>(old_value);
            } while (!m_event.m_state.compare_exchange_weak(
                old_value, this, std::memory_order_release, std::memory_order_acquire));

            return true;
        }

        auto await_resume() noexcept {}

        const event&            m_event;
        std::coroutine_handle<> m_awaiting_coroutine;
        awaiter*                m_next{nullptr};
    };

    auto operator co_await() const noexcept -> awaiter { return awaiter(*this); }

    auto reset() noexcept -> void
    {
        void* old_value = this;
        m_state.compare_exchange_strong(old_value, nullptr, std::memory_order_acquire);
    }

  protected:
    friend struct awaiter;
    mutable std::atomic<void*> m_state;
};

} // namespace coro

#pragma once

#include <coroutine>
#include <optional>
#include <atomic>

namespace coro
{

template<typename return_type>
class async_manual_reset_event
{
public:
    async_manual_reset_event() noexcept
        : m_state(nullptr)
    {
    }

    async_manual_reset_event(const async_manual_reset_event&) = delete;
    async_manual_reset_event(async_manual_reset_event&&) = delete;
    auto operator=(const async_manual_reset_event&) -> async_manual_reset_event& = delete;
    auto operator=(async_manual_reset_event&&) -> async_manual_reset_event& = delete;

    bool is_set() const noexcept
    {
        return m_state.load(std::memory_order_acquire) == this;
    }

    struct awaiter
    {
        awaiter(const async_manual_reset_event& event) noexcept
            : m_event(event)
        {

        }

        auto await_ready() const noexcept -> bool
        {
            return m_event.is_set();
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            const void* const set_state = &m_event;

            m_awaiting_coroutine = awaiting_coroutine;

            // This value will update if other threads write to it via acquire.
            void* old_value = m_event.m_state.load(std::memory_order_acquire);
            do
            {
                // Resume immediately if already in the set state.
                if(old_value == set_state)
                {
                    return false;
                }

                m_next = static_cast<awaiter*>(old_value);
            } while(!m_event.m_state.compare_exchange_weak(
                old_value,
                this,
                std::memory_order_release,
                std::memory_order_acquire));

            return true;
        }

        auto await_resume() noexcept
        {

        }

        auto return_value() const & -> const return_type&
        {
            return m_event.m_return_value;
        }

        const async_manual_reset_event& m_event;
        std::coroutine_handle<> m_awaiting_coroutine;
        awaiter* m_next{nullptr};
    };

    auto operator co_await() const noexcept -> awaiter
    {
        return awaiter(*this);
    }

    auto set(return_type return_value) noexcept -> void
    {
        void* old_value = m_state.exchange(this, std::memory_order_acq_rel);
        if(old_value != this)
        {
            m_return_value = std::move(return_value);

            auto* waiters = static_cast<awaiter*>(old_value);
            while(waiters != nullptr)
            {
                auto* next = waiters->m_next;
                waiters->m_awaiting_coroutine.resume();
                waiters = next;
            }
        }
    }

    auto reset() noexcept -> void
    {
        void* old_value = this;
        m_state.compare_exchange_strong(old_value, nullptr, std::memory_order_acquire);
    }

    auto return_value() const -> const return_type&
    {
        return m_return_value;
    }

private:
    friend struct awaiter;
    return_type m_return_value;
    mutable std::atomic<void*> m_state;
};

} // namespace coro

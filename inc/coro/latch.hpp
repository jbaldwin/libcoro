#pragma once

#include "coro/event.hpp"

#include <atomic>

namespace coro
{
class latch
{
public:
    latch(std::ptrdiff_t count) noexcept : m_count(count), m_event(count <= 0) {}

    latch(const latch&) = delete;
    latch(latch&&)      = delete;
    auto operator=(const latch&) -> latch& = delete;
    auto operator=(latch &&) -> latch& = delete;

    auto is_ready() const noexcept -> bool { return m_event.is_set(); }

    auto remaining() const noexcept -> std::size_t { return m_count.load(std::memory_order::acquire); }

    auto count_down(std::ptrdiff_t n = 1) noexcept -> void
    {
        if (m_count.fetch_sub(n, std::memory_order::acq_rel) <= n)
        {
            m_event.set();
        }
    }

    auto operator co_await() const noexcept -> event::awaiter { return m_event.operator co_await(); }

private:
    std::atomic<std::ptrdiff_t> m_count;
    event                       m_event;
};

} // namespace coro

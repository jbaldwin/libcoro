#include "coro/sync_wait.hpp"

namespace coro::detail
{
sync_wait_event::sync_wait_event(bool initially_set) : m_set(initially_set)
{
}

auto sync_wait_event::set() noexcept -> void
{
    m_set.exchange(true, std::memory_order::release);
    m_cv.notify_all();
}

auto sync_wait_event::reset() noexcept -> void
{
    m_set.exchange(false, std::memory_order::release);
}

auto sync_wait_event::wait() noexcept -> void
{
    std::unique_lock<std::mutex> lk{m_mutex};
    m_cv.wait(lk, [this] { return m_set.load(std::memory_order::acquire); });
}

} // namespace coro::detail

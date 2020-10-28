#include "coro/sync_wait.hpp"

namespace coro::detail
{
sync_wait_event::sync_wait_event(bool initially_set) : m_set(initially_set)
{
}

auto sync_wait_event::set() noexcept -> void
{
    {
        std::lock_guard<std::mutex> g{m_mutex};
        m_set = true;
    }

    m_cv.notify_all();
}

auto sync_wait_event::reset() noexcept -> void
{
    std::lock_guard<std::mutex> g{m_mutex};
    m_set = false;
}

auto sync_wait_event::wait() noexcept -> void
{
    std::unique_lock<std::mutex> lk{m_mutex};
    m_cv.wait(lk, [this] { return m_set; });
}

} // namespace coro::detail
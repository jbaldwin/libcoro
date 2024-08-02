#include "coro/sync_wait.hpp"

namespace coro::detail
{
sync_wait_event::sync_wait_event(bool initially_set) : m_set(initially_set)
{
}

auto sync_wait_event::set() noexcept -> void
{
    // issue-270 100~ task's on a thread_pool within sync_wait(when_all(tasks)) can cause a deadlock/hang if using
    // release/acquire or even seq_cst.
    m_set.exchange(true, std::memory_order::seq_cst);
    std::unique_lock<std::mutex> lk{m_mutex};
    m_cv.notify_all();
}

auto sync_wait_event::reset() noexcept -> void
{
    m_set.exchange(false, std::memory_order::seq_cst);
}

auto sync_wait_event::wait() noexcept -> void
{
    std::unique_lock<std::mutex> lk{m_mutex};
    m_cv.wait(lk, [this] { return m_set.load(std::memory_order::seq_cst); });
}

} // namespace coro::detail

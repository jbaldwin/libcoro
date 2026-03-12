#include "coro/detail/io_notifier_iocp.hpp"
#include "coro/detail/timer_handle.hpp"
#include <stdexcept>

auto coro::detail::io_notifier_iocp::watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
    throw std::runtime_error("watch_timer implemented");
}
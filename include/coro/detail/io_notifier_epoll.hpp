#pragma once

#include <chrono>
#include <ctime>
#include <vector>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "coro/detail/poll_info.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"

namespace coro::detail
{

using event_t = struct ::epoll_event;

class timer_handle;

class io_notifier_epoll
{
    static const constexpr std::size_t m_max_events = 16;

    fd_t m_fd;

    friend class detail::timer_handle;

    static auto event_to_poll_status(const event_t& event) -> poll_status;

public:
    io_notifier_epoll();

    io_notifier_epoll(const io_notifier_epoll&)                    = delete;
    io_notifier_epoll(io_notifier_epoll&&)                         = delete;
    auto operator=(const io_notifier_epoll&) -> io_notifier_epoll& = delete;
    auto operator=(io_notifier_epoll&&) -> io_notifier_epoll&      = delete;

    ~io_notifier_epoll();

    auto watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool;

    auto watch(fd_t fd, poll_op op, void* data, bool keep = false, bool is_cancel_event = false) -> bool;

    auto watch(poll_info& pi) -> bool;

    auto unwatch(fd_t fd, poll_op op) -> bool;

    auto unwatch(detail::poll_info& pi) -> bool;

    auto unwatch_timer(const timer_handle& timer) -> bool;

    auto next_events(std::vector<std::pair<poll_info*, poll_status>>& ready_events, std::chrono::milliseconds timeout)
        -> void;
};

} // namespace coro::detail

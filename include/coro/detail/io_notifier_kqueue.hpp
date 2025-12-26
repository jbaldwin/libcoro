#pragma once

#include <chrono>
#include <ctime>
#include <vector>

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "coro/detail/poll_info.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"

namespace coro::detail
{

using event_t = struct ::kevent;

class timer_handle;

class io_notifier_kqueue
{
    static const constexpr std::size_t m_max_events = 16;

    fd_t m_fd;

    friend class detail::timer_handle;

    static auto event_to_poll_status(const event_t& event) -> poll_status;

public:
    io_notifier_kqueue();

    io_notifier_kqueue(const io_notifier_kqueue&)                    = delete;
    io_notifier_kqueue(io_notifier_kqueue&&)                         = delete;
    auto operator=(const io_notifier_kqueue&) -> io_notifier_kqueue& = delete;
    auto operator=(io_notifier_kqueue&&) -> io_notifier_kqueue&      = delete;

    ~io_notifier_kqueue();

    auto watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool;

    auto watch(fd_t fd, poll_op op, void* data, bool keep = false) -> bool;

    auto watch(poll_info& pi) -> bool;

    auto unwatch(fd_t fd, poll_op op) -> bool;

    auto unwatch(poll_info& pi) -> bool;

    auto unwatch_timer(const timer_handle& timer) -> bool;

    auto next_events(std::vector<std::pair<poll_info*, poll_status>>& ready_events, std::chrono::milliseconds timeout)
        -> void;

    auto native_handle() const -> fd_t { return m_fd; }
};

} // namespace coro::detail

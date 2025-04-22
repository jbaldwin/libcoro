#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <vector>

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include <sys/event.h>
    #include <sys/time.h>
#elif defined(__linux__)
    #include <sys/epoll.h>
    #include <sys/timerfd.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#include "coro/detail/poll_info.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"
#include "detail/poll_info.hpp"

using namespace std::chrono_literals;

namespace coro
{

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
using event_t = struct ::kevent;
#elif defined(__linux__)
using event_t = struct ::epoll_event;
#endif

class io_notifier;

class timer_handle
{
    fd_t        m_fd;
    const void* m_timer_handle_ptr = nullptr;

    friend class io_notifier;

public:
    timer_handle(const void* timer_handle_ptr, io_notifier&);
};

class io_notifier
{
    static const constexpr std::size_t m_max_events = 16;
    fd_t                               m_fd;

    friend class timer_handle;

public:
    io_notifier();

    io_notifier(const io_notifier&)                    = delete;
    io_notifier(io_notifier&&)                         = delete;
    auto operator=(const io_notifier&) -> io_notifier& = delete;
    auto operator=(io_notifier&&) -> io_notifier&      = delete;

    ~io_notifier();

    auto watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool;

    auto watch(fd_t fd, coro::poll_op op, void* data, bool keep = false) -> bool;

    auto watch(detail::poll_info& pi) -> bool;

    auto unwatch(detail::poll_info& pi) -> bool;

    auto unwatch_timer(const timer_handle& timer) -> bool;

    auto next_events(std::chrono::milliseconds timeout)
        -> std::vector<std::pair<detail::poll_info*, coro::poll_status>>;

    static auto event_to_poll_status(const event_t& event) -> poll_status;
};

} // namespace coro

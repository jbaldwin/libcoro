#include "coro/io_notifier.hpp"

#include <array>
#include <stdexcept>

namespace coro
{

io_notifier::io_notifier()
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    : m_fd{::kqueue()}
#elif defined(__linux__)
    : m_fd{::epoll_create1(EPOLL_CLOEXEC)}
#endif
{
}

io_notifier::~io_notifier()
{
    if (m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

timer_handle::timer_handle(const void* timer_handle_ptr, io_notifier& notifier)
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    : m_fd{1},
#elif defined(__linux__)
    : m_fd(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
#endif
      m_timer_handle_ptr(timer_handle_ptr)
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    (void)notifier;
#elif defined(__linux__)
    notifier.watch(m_fd, coro::poll_op::read, const_cast<void*>(m_timer_handle_ptr), true);
#endif
}

auto io_notifier::watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    auto event_data = event_t{};
    EV_SET(
        &event_data,
        timer.m_fd,
        EVFILT_TIMER,
        EV_ADD | EV_ONESHOT,
        NOTE_NSECONDS,
        duration.count(),
        const_cast<void*>(timer.m_timer_handle_ptr));
    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
#elif defined(__linux__)
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    duration -= seconds;
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);

    // As a safeguard if both values end up as zero (or negative) then trigger the timeout
    // immediately as zero disarms timerfd according to the man pages and negative values
    // will result in an error return value.
    if (seconds <= 0s)
    {
        seconds = 0s;
        if (nanoseconds <= 0ns)
        {
            nanoseconds = 1ns;
        }
    }

    itimerspec ts{};
    ts.it_value.tv_sec  = seconds.count();
    ts.it_value.tv_nsec = nanoseconds.count();
    return ::timerfd_settime(timer.m_fd, 0, &ts, nullptr) != -1;
#endif
}

auto io_notifier::watch(fd_t fd, coro::poll_op op, void* data, bool keep) -> bool
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    auto event_data = event_t{};
    auto mode       = EV_ADD | EV_ENABLE;
    if (!keep)
    {
        mode |= EV_ONESHOT;
    }
    EV_SET(&event_data, fd, static_cast<int16_t>(op), mode, 0, 0, data);
    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
#elif defined(__linux__)
    auto event_data     = event_t{};
    event_data.events   = static_cast<uint32_t>(op) | EPOLLRDHUP;
    event_data.data.ptr = data;
    if (!keep)
    {
        event_data.events |= EPOLLONESHOT;
    }
    return ::epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event_data) != -1;
#endif
}

auto io_notifier::watch(detail::poll_info& pi) -> bool
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    // For read-write event, we need to register both event types separately to the kqueue
    if (pi.m_op == coro::poll_op::read_write)
    {
        auto event_data = event_t{};

        EV_SET(
            &event_data,
            pi.m_fd,
            static_cast<int16_t>(coro::poll_op::read),
            EV_ADD | EV_ONESHOT | EV_ENABLE,
            0,
            0,
            static_cast<void*>(&pi));
        ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr);

        EV_SET(
            &event_data,
            pi.m_fd,
            static_cast<int16_t>(coro::poll_op::write),
            EV_ADD | EV_ONESHOT | EV_ENABLE,
            0,
            0,
            static_cast<void*>(&pi));
        ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr);

        return true;
    }
    else
    {
        auto event_data = event_t{};
        EV_SET(
            &event_data,
            pi.m_fd,
            static_cast<int16_t>(pi.m_op),
            EV_ADD | EV_ONESHOT | EV_ENABLE,
            0,
            0,
            static_cast<void*>(&pi));
        return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
    }
#elif defined(__linux__)
    auto event_data     = event_t{};
    event_data.events   = static_cast<uint32_t>(pi.m_op) | EPOLLONESHOT | EPOLLRDHUP;
    event_data.data.ptr = static_cast<void*>(&pi);
    return ::epoll_ctl(m_fd, EPOLL_CTL_ADD, pi.m_fd, &event_data) != -1;
#endif
}

auto io_notifier::unwatch(detail::poll_info& pi) -> bool
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    // For read-write event, we need to register both event types separately to the kqueue
    if (pi.m_op == coro::poll_op::read_write)
    {
        auto event_data = event_t{};

        EV_SET(&event_data, pi.m_fd, static_cast<int16_t>(coro::poll_op::read), EV_DELETE, 0, 0, nullptr);
        ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr);

        EV_SET(&event_data, pi.m_fd, static_cast<int16_t>(coro::poll_op::write), EV_DELETE, 0, 0, nullptr);
        ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr);

        return true;
    }
    else
    {
        auto event_data = event_t{};
        EV_SET(&event_data, pi.m_fd, static_cast<int16_t>(pi.m_op), EV_DELETE, 0, 0, nullptr);
        return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
    }
#elif defined(__linux__)
    return ::epoll_ctl(m_fd, EPOLL_CTL_DEL, pi.m_fd, nullptr) != -1;
#endif
}

auto io_notifier::unwatch_timer(const timer_handle& timer) -> bool
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    auto event_data = event_t{};
    EV_SET(&event_data, timer.m_fd, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
#elif defined(__linux__)
    // Setting these values to zero disables the timer.
    itimerspec ts{};
    ts.it_value.tv_sec  = 0;
    ts.it_value.tv_nsec = 0;
    return ::timerfd_settime(timer.m_fd, 0, &ts, nullptr) != -1;
#endif
}

auto io_notifier::next_events(std::chrono::milliseconds timeout)
    -> std::vector<std::pair<detail::poll_info*, coro::poll_status>>
{
    auto ready_events = std::vector<std::pair<detail::poll_info*, coro::poll_status>>();
    auto ready_set    = std::array<event_t, m_max_events>{};
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    const auto timeout_as_secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto       timeout_spec    = ::timespec{
                 .tv_sec  = timeout_as_secs.count(),
                 .tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - timeout_as_secs).count(),
    };
    const std::size_t num_ready = ::kevent(m_fd, nullptr, 0, ready_set.data(), ready_set.size(), &timeout_spec);
    for (std::size_t i = 0; i < num_ready; i++)
    {
        ready_events.emplace_back(
            static_cast<detail::poll_info*>(ready_set[i].udata), io_notifier::event_to_poll_status(ready_set[i]));
    }
#elif defined(__linux__)
    int num_ready = ::epoll_wait(m_fd, ready_set.data(), ready_set.size(), timeout.count());
    for (int i = 0; i < num_ready; ++i)
    {
        ready_events.emplace_back(
            static_cast<detail::poll_info*>(ready_set[i].data.ptr), io_notifier::event_to_poll_status(ready_set[i]));
    }
#endif
    return ready_events;
}

auto io_notifier::event_to_poll_status(const event_t& event) -> poll_status
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    if (event.filter & EVFILT_READ || event.filter & EVFILT_WRITE)
    {
        return poll_status::event;
    }
    else if (event.flags & EV_ERROR)
    {
        return poll_status::error;
    }
    else if (event.flags & EV_EOF)
    {
        return poll_status::closed;
    }
    throw std::runtime_error{"invalid kqueue state"};
#elif defined(__linux__)
    if (event.events & static_cast<uint32_t>(poll_op::read) || event.events & static_cast<uint32_t>(poll_op::write))
    {
        return poll_status::event;
    }
    else if (event.events & EPOLLERR)
    {
        return poll_status::error;
    }
    else if (event.events & EPOLLRDHUP || event.events & EPOLLHUP)
    {
        return poll_status::closed;
    }
    throw std::runtime_error{"invalid epoll state"};
#endif
}

} // namespace coro

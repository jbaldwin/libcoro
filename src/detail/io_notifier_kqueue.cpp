#include "coro/detail/io_notifier_kqueue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <iostream>
#include <stdexcept>

#include "coro/detail/timer_handle.hpp"

using namespace std::chrono_literals;

namespace coro::detail
{

io_notifier_kqueue::io_notifier_kqueue() : m_fd{::kqueue()}
{
}

io_notifier_kqueue::~io_notifier_kqueue()
{
    if (m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

auto io_notifier_kqueue::watch_timer(const detail::timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
    // Prevent negative durations for the timeout as they will result in an error. 0 will fire in the next instance
    // possible.
    if (duration < 0ns)
    {
        duration = 0ns;
    }

    auto event_data = event_t{};
    EV_SET(
        &event_data,
        timer.get_fd(),
        EVFILT_TIMER,
        EV_ADD | EV_CLEAR | EV_ONESHOT,
        NOTE_NSECONDS,
        duration.count(),
        const_cast<void*>(timer.get_inner()));

    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
}

auto io_notifier_kqueue::watch(fd_t fd, coro::poll_op op, void* data, bool keep) -> bool
{
    auto event_data = event_t{};
    auto mode       = EV_ADD | EV_CLEAR | EV_ENABLE;
    if (!keep)
    {
        mode |= EV_ONESHOT;
    }
    else
    {
        // For events being kept in a scheduler they need to be edge triggered or they'll constantly wake-up the event loop.
        mode |= EV_CLEAR;
    }
    EV_SET(&event_data, fd, static_cast<int16_t>(op), mode, 0, 0, data);
    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
}

auto io_notifier_kqueue::watch(detail::poll_info& pi) -> bool
{
    // For read-write event, we need to register both event types separately to the kqueue
    if (pi.m_op == coro::poll_op::read_write)
    {
        auto event_data = event_t{};

        EV_SET(
            &event_data,
            pi.m_fd,
            static_cast<int16_t>(coro::poll_op::read),
            EV_ADD | EV_CLEAR | EV_ONESHOT | EV_ENABLE,
            0,
            0,
            static_cast<void*>(&pi));
        ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr);

        EV_SET(
            &event_data,
            pi.m_fd,
            static_cast<int16_t>(coro::poll_op::write),
            EV_ADD | EV_CLEAR | EV_ONESHOT | EV_ENABLE,
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
            EV_ADD | EV_CLEAR | EV_ONESHOT | EV_ENABLE,
            0,
            0,
            static_cast<void*>(&pi));
        return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
    }
}

auto io_notifier_kqueue::unwatch(detail::poll_info& pi) -> bool
{
    // For read-write event, we need to de-register both event types separately to the kqueue
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
}

auto io_notifier_kqueue::unwatch_timer(const detail::timer_handle& timer) -> bool
{
    auto event_data = event_t{};
    EV_SET(&event_data, timer.get_fd(), EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
    return ::kevent(m_fd, &event_data, 1, nullptr, 0, nullptr) != -1;
}

auto io_notifier_kqueue::next_events(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events, std::chrono::milliseconds timeout)
    -> void
{
    auto       ready_set       = std::array<event_t, m_max_events>{};
    const auto timeout_as_secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto       timeout_spec    = ::timespec{
                 .tv_sec  = timeout_as_secs.count(),
                 .tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - timeout_as_secs).count(),
    };
    const int num_ready = ::kevent(
        m_fd, nullptr, 0, ready_set.data(), std::min(ready_set.size(), ready_events.capacity()), &timeout_spec);
    for (std::size_t i = 0; i < num_ready; i++)
    {
        ready_events.emplace_back(
            static_cast<detail::poll_info*>(ready_set[i].udata),
            io_notifier_kqueue::event_to_poll_status(ready_set[i]));
    }
}

auto io_notifier_kqueue::event_to_poll_status(const event_t& event) -> poll_status
{
    if ((event.filter == EVFILT_READ || event.filter == EVFILT_WRITE || event.filter == EVFILT_TIMER) &&
        event.flags & EV_EOF)
    {
        return poll_status::closed;
    }
    else if (
        (event.filter == EVFILT_READ || event.filter == EVFILT_WRITE || event.filter == EVFILT_TIMER) &&
        event.flags & EV_ERROR)
    {
        return poll_status::error;
    }
    else if (event.filter == EVFILT_READ)
    {
        return poll_status::read;
    }
    else if (event.filter == EVFILT_WRITE)
    {
        return poll_status::write;
    }
    else if (event.filter == EVFILT_TIMER)
    {
        // Due timer is handled like a read event by the lib
        return poll_status::read;
    }

    throw std::runtime_error{"invalid kqueue state"};
}

} // namespace coro::detail

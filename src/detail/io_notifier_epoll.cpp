#include "coro/detail/io_notifier_epoll.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>

#include "coro/detail/timer_handle.hpp"

using namespace std::chrono_literals;

namespace coro::detail
{

io_notifier_epoll::io_notifier_epoll() : m_fd{::epoll_create1(EPOLL_CLOEXEC)}
{
}

io_notifier_epoll::~io_notifier_epoll()
{
    if (m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

auto io_notifier_epoll::watch_timer(const detail::timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
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
    return ::timerfd_settime(timer.get_fd(), 0, &ts, nullptr) != -1;
}

auto io_notifier_epoll::watch(fd_t fd, coro::poll_op op, void* data, bool keep) -> bool
{
    auto event_data     = event_t{};
    event_data.events   = static_cast<uint32_t>(op) | EPOLLRDHUP;
    event_data.data.ptr = data;
    if (!keep)
    {
        event_data.events |= EPOLLONESHOT;
    }
    else
    {
        // For events being kept in a scheduler they need to be edge triggered or they'll constantly wake-up the event loop.
        event_data.events |= EPOLLET;
    }
    return ::epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event_data) != -1;
}

auto io_notifier_epoll::watch(detail::poll_info& pi) -> bool
{
    auto event_data     = event_t{};
    event_data.events   = static_cast<uint32_t>(pi.m_op) | EPOLLONESHOT | EPOLLRDHUP | EPOLLHUP;
    event_data.data.ptr = static_cast<void*>(&pi);
    return ::epoll_ctl(m_fd, EPOLL_CTL_ADD, pi.m_fd, &event_data) != -1;
}

auto io_notifier_epoll::unwatch(detail::poll_info& pi) -> bool
{
    return ::epoll_ctl(m_fd, EPOLL_CTL_DEL, pi.m_fd, nullptr) != -1;
}

auto io_notifier_epoll::unwatch_timer(const detail::timer_handle& timer) -> bool
{
    // Setting these values to zero disables the timer.
    itimerspec ts{};
    ts.it_value.tv_sec  = 0;
    ts.it_value.tv_nsec = 0;
    return ::timerfd_settime(timer.get_fd(), 0, &ts, nullptr) != -1;
}

auto io_notifier_epoll::next_events(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events, std::chrono::milliseconds timeout)
    -> void
{
    auto ready_set = std::array<event_t, m_max_events>{};
    int  num_ready = ::epoll_wait(m_fd, ready_set.data(), ready_set.size(), timeout.count());
    for (int i = 0; i < num_ready; ++i)
    {
        ready_events.emplace_back(
            static_cast<detail::poll_info*>(ready_set[i].data.ptr),
            io_notifier_epoll::event_to_poll_status(ready_set[i]));
    }
}

auto io_notifier_epoll::event_to_poll_status(const event_t& event) -> poll_status
{
    if (event.events & static_cast<uint32_t>(poll_op::read))
    {
        return poll_status::read;
    }
    if (event.events & static_cast<uint32_t>(poll_op::write))
    {
        return poll_status::write;
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
}

} // namespace coro::detail

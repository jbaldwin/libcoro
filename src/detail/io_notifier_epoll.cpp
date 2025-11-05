#include "coro/detail/io_notifier_epoll.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>

#include "coro/detail/poll_info.hpp"
#include "coro/detail/timer_handle.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"

using namespace std::chrono_literals;

namespace coro::detail
{

/**
 * Encode the state needed to rewind the correct poll info after an event occured into the user data of an epoll event.
 *
 * We need a pointer to the poll_info awaitable to resume the awaiting coroutine. Additionally we need to know if the
 * captured event came from the file descriptor of the poll_info, in which case we want to decode the poll_status from
 * the event, or from a registered cancellation token, in which case we want to return a cancelled poll status.
 */
auto encode_udata(bool keep_registered, bool is_cancel_event, void* udata) -> uint64_t
{
    // Pointers on 64 bit unix machines take up to 48 bit right now. So we have some bits left to encode the boolean to
    // indicate if this is a cancellation event descriptor at the highest bit.
    return (((uint64_t)keep_registered) << 63) | (((uint64_t)is_cancel_event) << 62) |
           (reinterpret_cast<uintptr_t>(udata) & 0x3FFFFFFFFFFFFFFFULL);
}

/**
 * Decode the state that was encoded into the epoll events user data field.
 *
 * For details see documentation of `coro::detail::encode_udata(bool, bool, void*)` above.
 */
auto decode_udata(uint64_t encoded) -> std::tuple<bool, bool, void*>
{
    bool  keep_registered = (bool)(encoded >> 63);
    bool  is_cancel_event = (bool)((encoded >> 62) & 0x1);
    void* udata           = reinterpret_cast<void*>(encoded & 0xFFFFFFFFFFFFULL);
    return std::make_tuple(keep_registered, is_cancel_event, udata);
}

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

auto io_notifier_epoll::watch_timer(const timer_handle& timer, std::chrono::nanoseconds duration) -> bool
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

auto io_notifier_epoll::watch(fd_t fd, poll_op op, void* data, bool keep, bool is_cancel_event) -> bool
{
    auto event_data     = event_t{};
    event_data.events   = static_cast<uint32_t>(op) | EPOLLRDHUP;
    event_data.data.u64 = encode_udata(keep, is_cancel_event, data);

    if (!keep)
    {
        event_data.events |= EPOLLONESHOT;
    }
    else
    {
        // For events being kept in a scheduler they need to be edge triggered or they'll constantly wake-up the event
        // loop.
        event_data.events |= EPOLLET;
    }

    return ::epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event_data) != -1;
}

auto io_notifier_epoll::watch(poll_info& pi) -> bool
{
    watch(pi.m_fd, pi.m_op, static_cast<void*>(&pi), false, false);

    if (pi.m_cancel_trigger.has_value())
    {
        watch(pi.m_cancel_trigger.value().native_handle(), poll_op::read, static_cast<void*>(&pi), false, true);
    }

    return true;
}

auto io_notifier_epoll::unwatch(fd_t fd, poll_op op) -> bool
{
    return ::epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, nullptr) != -1;
}

auto io_notifier_epoll::unwatch(poll_info& pi) -> bool
{
    return unwatch(pi.m_fd, pi.m_op);
}

auto io_notifier_epoll::unwatch_timer(const timer_handle& timer) -> bool
{
    // Setting these values to zero disables the timer.
    itimerspec ts{};
    ts.it_value.tv_sec  = 0;
    ts.it_value.tv_nsec = 0;
    return ::timerfd_settime(timer.get_fd(), 0, &ts, nullptr) != -1;
}

auto io_notifier_epoll::next_events(
    std::vector<std::pair<poll_info*, poll_status>>& ready_events, std::chrono::milliseconds timeout) -> void
{
    auto ready_set = std::array<event_t, m_max_events>{};
    int  num_ready = ::epoll_wait(m_fd, ready_set.data(), ready_set.size(), timeout.count());
    for (int i = 0; i < num_ready; ++i)
    {
        auto [keep_registered, is_cancel_event, udata] = decode_udata(ready_set[i].data.u64);
        auto* pi                                       = static_cast<poll_info*>(udata);

        // If the event issuing fd is the same as the fd of the cancellation trigger of the registered poll_info we
        // this operation was cancelled by the user.
        if (is_cancel_event)
        {
            ready_events.emplace_back(pi, poll_status::cancelled);
            if (!keep_registered)
            {
                unwatch(*pi);
            }
        }
        else
        {
            ready_events.emplace_back(pi, io_notifier_epoll::event_to_poll_status(ready_set[i]));
            if (pi->m_cancel_trigger.has_value() && !keep_registered)
            {
                unwatch(pi->m_cancel_trigger.value().native_handle(), poll_op::read);
            }
        }
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

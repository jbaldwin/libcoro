#pragma once

#include <array>
#include <string>

#if defined(__linux__)
    #include <sys/epoll.h>
#endif
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include <sys/event.h>
#endif
#include <unistd.h>

#include "coro/detail/pipe.hpp"
#include "coro/fd.hpp"

namespace coro
{
#if defined(__linux__)
enum class poll_op : uint64_t
{
    /// Poll for read operations.
    read = EPOLLIN,
    /// Poll for write operations.
    write = EPOLLOUT,
    /// Poll for read and write operations.
    read_write = EPOLLIN | EPOLLOUT
};
#endif
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
enum class poll_op : int64_t
{
    /// Poll for read operations.
    read = EVFILT_READ,
    /// Poll for write operations.
    write = EVFILT_WRITE,
    /// Poll for read and write operations.
    read_write = -5,
};
#endif

inline auto poll_op_readable(poll_op op) -> bool
{
    return (static_cast<int64_t>(op) & static_cast<int64_t>(poll_op::read));
}

inline auto poll_op_writeable(poll_op op) -> bool
{
    return (static_cast<int64_t>(op) & static_cast<int64_t>(poll_op::write));
}

auto to_string(poll_op op) -> const std::string&;

enum class poll_status
{
    /// The poll operation was was successful with a read-event.
    read,
    /// The poll operation was was successful with a write-event.
    write,
    /// The poll operation timed out.
    timeout,
    /// The file descriptor had an error while polling.
    error,
    /// The file descriptor has been closed by the remote or an internal error/close.
    closed,
    /// The poll operation was cancelled by a 'poll_stop_source'.
    cancelled,
};

auto to_string(poll_status status) -> const std::string&;

class poll_stop_token
{
    fd_t m_receiver{-1};

public:
    explicit poll_stop_token(fd_t receiver) : m_receiver(receiver) {}

    auto native_handle() const -> fd_t { return m_receiver; }
};

class poll_stop_source
{
    detail::pipe_t m_pipe{};

public:
    poll_stop_source() = default;

    poll_stop_source(const poll_stop_source&) = delete;
    poll_stop_source(poll_stop_source&& other) { *this = std::move(other); }

    poll_stop_source& operator=(const poll_stop_source&) = delete;
    poll_stop_source& operator=(poll_stop_source&& other)
    {
        m_pipe = std::move(other.m_pipe);
        return *this;
    }

    ~poll_stop_source() = default;

    auto get_token() const -> poll_stop_token { return poll_stop_token(m_pipe.read_fd()); }

    auto signal_stop() -> void
    {
        const int value{1};
        ::write(m_pipe.write_fd(), reinterpret_cast<const void*>(&value), sizeof(value));
    }
};

} // namespace coro

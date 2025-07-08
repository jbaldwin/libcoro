#pragma once

#include "platform.hpp"
#include <string>
#if defined(CORO_PLATFORM_LINUX)
    #include <sys/epoll.h>
#endif
#if defined(CORO_PLATFORM_BSD)
    #include <sys/event.h>
#endif

namespace coro
{
#if defined(CORO_PLATFORM_LINUX)
enum class poll_op : uint64_t
{
    /// Poll for read operations.
    read = EPOLLIN,
    /// Poll for write operations.
    write = EPOLLOUT,
    /// Poll for read and write operations.
    read_write = EPOLLIN | EPOLLOUT
};
#elif defined(CORO_PLATFORM_BSD)
enum class poll_op : int64_t
{
    /// Poll for read operations.
    read = EVFILT_READ,
    /// Poll for write operations.
    write = EVFILT_WRITE,
    /// Poll for read and write operations.
    // read_write = EVFILT_READ | EVFILT_WRITE
    read_write = -5
};
#elif defined(CORO_PLATFORM_WINDOWS)
// Windows doesn't have polling, so we don't use it.
enum class poll_op : uint32_t
{
    read,
    write,
    read_write
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
    /// The poll operation was was successful.
    event,
    /// The poll operation timed out.
    timeout,
    /// The file descriptor had an error while polling.
    error,
    /// The file descriptor has been closed by the remote or an internal error/close.
    closed
};

auto to_string(poll_status status) -> const std::string&;

} // namespace coro

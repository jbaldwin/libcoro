#pragma once

#include <sys/epoll.h>

namespace coro
{
enum class poll_op
{
    /// Poll for read operations.
    read = EPOLLIN,
    /// Poll for write operations.
    write = EPOLLOUT,
    /// Poll for read and write operations.
    read_write = EPOLLIN | EPOLLOUT
};

enum class poll_status
{
    /// The poll operation was was successful.
    success,
    /// The poll operation timed out.
    timeout,
    /// The file descriptor had an error while polling.
    error,
    /// The file descriptor has been closed by the remote or an internal error/close.
    closed
};

} // namespace coro

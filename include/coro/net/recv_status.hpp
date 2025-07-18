#pragma once

#include <cstdint>
#include <errno.h>
#include <string>

namespace coro::net
{
enum class recv_status : int64_t
{
    ok = 0,
    /// The peer closed the socket.
    closed = -1,
    /// The udp socket has not been bind()'ed to a local port.
    udp_not_bound = -2,
    try_again     = EAGAIN,
    // Note: that only the tcp::client will return this, a tls::client returns the specific ssl_would_block_* status'.
    would_block              = EWOULDBLOCK,
    bad_file_descriptor      = EBADF,
    connection_refused       = ECONNREFUSED,
    memory_fault             = EFAULT,
    interrupted              = EINTR,
    invalid_argument         = EINVAL,
    no_memory                = ENOMEM,
    not_connected            = ENOTCONN,
    not_a_socket             = ENOTSOCK,
    connection_reset_by_peer = ECONNRESET,
};

auto to_string(recv_status status) -> const std::string&;

} // namespace coro::net

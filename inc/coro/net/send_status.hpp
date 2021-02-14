#pragma once

#include <cstdint>
#include <errno.h>

namespace coro::net
{
enum class send_status : int64_t
{
    ok                       = 0,
    permission_denied        = EACCES,
    try_again                = EAGAIN,
    would_block              = EWOULDBLOCK,
    already_in_progress      = EALREADY,
    bad_file_descriptor      = EBADF,
    connection_reset         = ECONNRESET,
    no_peer_address          = EDESTADDRREQ,
    memory_fault             = EFAULT,
    interrupted              = EINTR,
    is_connection            = EISCONN,
    message_size             = EMSGSIZE,
    output_queue_full        = ENOBUFS,
    no_memory                = ENOMEM,
    not_connected            = ENOTCONN,
    not_a_socket             = ENOTSOCK,
    operationg_not_supported = EOPNOTSUPP,
    pipe_closed              = EPIPE,

    ssl_error = -3
};

} // namespace coro::net

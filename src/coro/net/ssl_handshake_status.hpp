#pragma once

namespace coro::net
{
enum class ssl_handshake_status
{
    /// The ssl handshake was successful.
    ok,
    /// The connection hasn't been established yet, use connect() prior to the ssl_handshake().
    not_connected,
    /// The connection needs a coro::net::ssl_context to perform the handshake.
    ssl_context_required,
    /// The internal ssl memory alocation failed.
    ssl_resource_allocation_failed,
    /// Attempting to set the connections ssl socket/file descriptor failed.
    ssl_set_fd_failure,
    /// The handshake had an error.
    handshake_failed,
    /// The handshake timed out.
    timeout,
    /// An error occurred while polling for read or write operations on the socket.
    poll_error,
    /// The socket was unexpectedly closed while attempting the handshake.
    unexpected_close

};

} // namespace coro::net

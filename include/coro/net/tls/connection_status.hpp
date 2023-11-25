#ifdef LIBCORO_FEATURE_TLS

    #pragma once

    #include <string>

namespace coro::net::tls
{
enum class connection_status
{
    /// The tls connection was successful.
    connected,
    /// The connection hasn't been established yet, use connect() prior to the handshake().
    not_connected,
    /// The connection needs a coro::net::tls::context to perform the handshake.
    context_required,
    /// The internal ssl memory alocation failed.
    resource_allocation_failed,
    /// Attempting to set the connections ssl socket/file descriptor failed.
    set_fd_failure,
    /// The handshake had an error.
    handshake_failed,
    /// The connection timed out.
    timeout,
    /// An error occurred while polling for read or write operations on the socket.
    poll_error,
    /// The socket was unexpectedly closed while attempting the handshake.
    unexpected_close,
    /// The given ip address could not be parsed or is invalid.
    invalid_ip_address,
    /// There was an unrecoverable error, use errno to get more information on the specific error.
    error
};

auto to_string(connection_status status) -> const std::string&;

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS

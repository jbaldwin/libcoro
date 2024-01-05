#ifdef LIBCORO_FEATURE_TLS

    #pragma once

    #include <cstdint>
    #include <errno.h>
    #include <openssl/ssl.h>
    #include <string>

namespace coro::net::tls
{

enum class recv_status : int64_t
{
    ok = SSL_ERROR_NONE,
    // The user provided an 0 length buffer.
    buffer_is_empty = -3,
    timeout         = -4,
    /// The peer closed the socket.
    closed           = SSL_ERROR_ZERO_RETURN,
    error            = SSL_ERROR_SSL,
    want_read        = SSL_ERROR_WANT_READ,
    want_write       = SSL_ERROR_WANT_WRITE,
    want_connect     = SSL_ERROR_WANT_CONNECT,
    want_accept      = SSL_ERROR_WANT_ACCEPT,
    want_x509_lookup = SSL_ERROR_WANT_X509_LOOKUP,
    error_syscall    = SSL_ERROR_SYSCALL,

};

auto to_string(recv_status status) -> const std::string&;

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS
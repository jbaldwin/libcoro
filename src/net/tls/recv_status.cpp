#include <coro/net/tls/recv_status.hpp>

namespace coro::net::tls
{

static std::string recv_status_ok{"ok"};
static std::string recv_status_buffer_is_empty{"buffer_is_empty"};
static std::string recv_status_timeout{"timeout"};
static std::string recv_status_closed{"closed"};
static std::string recv_status_error{"error"};
static std::string recv_status_want_read{"want_read"};
static std::string recv_status_want_write{"want_write"};
static std::string recv_status_want_connect{"want_connect"};
static std::string recv_status_want_accept{"want_accept"};
static std::string recv_status_want_x509_lookup{"want_x509_lookup"};
static std::string recv_status_error_syscall{"error_syscall"};
static std::string recv_status_unknown{"unknown"};

auto to_string(recv_status status) -> const std::string&
{
    switch (status)
    {
        case recv_status::ok:
            return recv_status_ok;
        case recv_status::buffer_is_empty:
            return recv_status_buffer_is_empty;
        case recv_status::timeout:
            return recv_status_timeout;
        case recv_status::closed:
            return recv_status_closed;
        case recv_status::error:
            return recv_status_error;
        case recv_status::want_read:
            return recv_status_want_read;
        case recv_status::want_write:
            return recv_status_want_write;
        case recv_status::want_connect:
            return recv_status_want_connect;
        case recv_status::want_accept:
            return recv_status_want_accept;
        case recv_status::want_x509_lookup:
            return recv_status_want_x509_lookup;
        case recv_status::error_syscall:
            return recv_status_error_syscall;
    }

    return recv_status_unknown;
}

} // namespace coro::net::tls

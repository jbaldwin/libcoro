#include <coro/net/tls/send_status.hpp>

namespace coro::net::tls
{

static std::string send_status_ok{"ok"};
static std::string send_status_buffer_is_empty{"buffer_is_empty"};
static std::string send_status_timeout{"timeout"};
static std::string send_status_closed{"closed"};
static std::string send_status_error{"error"};
static std::string send_status_want_read{"want_read"};
static std::string send_status_want_write{"want_write"};
static std::string send_status_want_connect{"want_connect"};
static std::string send_status_want_accept{"want_accept"};
static std::string send_status_want_x509_lookup{"want_x509_lookup"};
static std::string send_status_error_syscall{"error_syscall"};
static std::string send_status_unknown{"unknown"};

auto to_string(send_status status) -> const std::string&
{
    switch (status)
    {
        case send_status::ok:
            return send_status_ok;
        case send_status::buffer_is_empty:
            return send_status_buffer_is_empty;
        case send_status::timeout:
            return send_status_timeout;
        case send_status::closed:
            return send_status_closed;
        case send_status::error:
            return send_status_error;
        case send_status::want_read:
            return send_status_want_read;
        case send_status::want_write:
            return send_status_want_write;
        case send_status::want_connect:
            return send_status_want_connect;
        case send_status::want_accept:
            return send_status_want_accept;
        case send_status::want_x509_lookup:
            return send_status_want_x509_lookup;
        case send_status::error_syscall:
            return send_status_error_syscall;
    }

    return send_status_unknown;
}

} // namespace coro::net::tls

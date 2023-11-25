#include "coro/net/tls/connection_status.hpp"

namespace coro::net::tls
{
static const std::string connection_status_connected                  = {"connected"};
static const std::string connection_status_not_connected              = {"not_connected"};
static const std::string connection_status_context_required           = {"context_required"};
static const std::string connection_status_resource_allocation_failed = {"resource_allocation_failed"};
static const std::string connection_status_set_fd_failure             = {"set_fd_failure"};
static const std::string connection_status_handshake_failed           = {"handshake_failed"};
static const std::string connection_status_timeout                    = {"timeout"};
static const std::string connection_status_poll_error                 = {"poll_error"};
static const std::string connection_status_unexpected_close           = {"unexpected_close"};
static const std::string connection_status_invalid_ip_address         = {"invalid_ip_address"};
static const std::string connection_status_error                      = {"error"};
static const std::string connection_status_unknown                    = {"unknown"};

auto to_string(connection_status status) -> const std::string&
{
    switch (status)
    {
        case connection_status::connected:
            return connection_status_connected;
        case connection_status::not_connected:
            return connection_status_not_connected;
        case connection_status::context_required:
            return connection_status_context_required;
        case connection_status::resource_allocation_failed:
            return connection_status_resource_allocation_failed;
        case connection_status::set_fd_failure:
            return connection_status_set_fd_failure;
        case connection_status::handshake_failed:
            return connection_status_handshake_failed;
        case connection_status::timeout:
            return connection_status_timeout;
        case connection_status::poll_error:
            return connection_status_poll_error;
        case connection_status::unexpected_close:
            return connection_status_unexpected_close;
        case connection_status::invalid_ip_address:
            return connection_status_invalid_ip_address;
        case connection_status::error:
            return connection_status_error;
        default:
            return connection_status_unknown;
    }
}

} // namespace coro::net::tls
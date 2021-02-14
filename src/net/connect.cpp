#include "coro/net/connect.hpp"

#include <stdexcept>

namespace coro::net
{
const static std::string connect_status_connected{"connected"};
const static std::string connect_status_invalid_ip_address{"invalid_ip_address"};
const static std::string connect_status_timeout{"timeout"};
const static std::string connect_status_error{"error"};
const static std::string connect_status_dns_lookup_failure{"dns_lookup_failure"};
const static std::string connect_status_ssl_handshake_failure{"ssl_handshake_failure"};

auto to_string(const connect_status& status) -> const std::string&
{
    switch (status)
    {
        case connect_status::connected:
            return connect_status_connected;
        case connect_status::invalid_ip_address:
            return connect_status_invalid_ip_address;
        case connect_status::timeout:
            return connect_status_timeout;
        case connect_status::error:
            return connect_status_error;
        case connect_status::dns_lookup_failure:
            return connect_status_dns_lookup_failure;
        case connect_status::ssl_handshake_failure:
            return connect_status_ssl_handshake_failure;
    }

    throw std::logic_error{"Invalid/unknown connect status."};
}

} // namespace coro::net

#include "coro/connect.hpp"

#include <stdexcept>

namespace coro
{
static std::string connect_status_connected{"connected"};
static std::string connect_status_invalid_ip_address{"invalid_ip_address"};
static std::string connect_status_timeout{"timeout"};
static std::string connect_status_error{"error"};
static std::string connect_status_dns_client_required{"dns_client_required"};
static std::string connect_status_dns_lookup_failure{"dns_lookup_failure"};

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
        case connect_status::dns_client_required:
            return connect_status_dns_client_required;
        case connect_status::dns_lookup_failure:
            return connect_status_dns_lookup_failure;
    }

    throw std::logic_error{"Invalid/unknown connect status."};
}

} // namespace coro

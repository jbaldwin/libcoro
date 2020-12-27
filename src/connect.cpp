#include "coro/connect.hpp"

namespace coro
{
static std::string connect_status_connected{"connected"};
static std::string connect_status_invalid_ip_address{"invalid_ip_address"};
static std::string connect_status_timeout{"timeout"};
static std::string connect_status_error{"error"};

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
    }
}

} // namespace coro

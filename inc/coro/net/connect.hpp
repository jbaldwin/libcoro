#pragma once

#include <string>

namespace coro::net
{
enum class connect_status
{
    /// The connection has been established.
    connected,
    /// The given ip address could not be parsed or is invalid.
    invalid_ip_address,
    /// The connection operation timed out.
    timeout,
    /// There was an error, use errno to get more information on the specific error.
    error
};

/**
 * @param status String representation of the connection status.
 * @throw std::logic_error If provided an invalid connect_status enum value.
 */
auto to_string(const connect_status& status) -> const std::string&;

} // namespace coro::net

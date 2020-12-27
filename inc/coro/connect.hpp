#pragma once

#include <string>

namespace coro
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

auto to_string(const connect_status& status) -> const std::string&;

} // namespace coro

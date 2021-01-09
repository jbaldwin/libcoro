#pragma once

#include "coro/net/hostname.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <variant>
#include <span>

namespace coro
{
class io_scheduler;
} // namespace coro

namespace coro::net
{

class udp_client
{
public:
    struct options
    {
        /// The ip address to connect to.  If using hostname then a dns client must be provided.
        net::ip_address address{net::ip_address::from_string("127.0.0.1")};
        /// The port to connect to.
        uint16_t port{8080};
    };

    udp_client(io_scheduler& scheduler, options opts = options{
        .address = {net::ip_address::from_string("127.0.0.1")},
        .port = 8080});
    udp_client(const udp_client&) = delete;
    udp_client(udp_client&&)      = default;
    auto operator=(const udp_client&) noexcept -> udp_client& = delete;
    auto operator=(udp_client&&) noexcept -> udp_client& = default;
    ~udp_client()                                        = default;

    auto sendto(
        const std::span<const char> buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<ssize_t>;

private:
    /// The scheduler that will drive this udp client.
    io_scheduler& m_io_scheduler;
    /// Options for what server to connect to.
    options m_options;
    /// The udp socket.
    net::socket m_socket{-1};
};

} // namespace coro::net

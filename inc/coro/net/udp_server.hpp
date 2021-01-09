#pragma once

#include "coro/net/socket.hpp"
#include "coro/net/udp_client.hpp"
#include "coro/io_scheduler.hpp"

#include <string>
#include <functional>
#include <span>
#include <optional>

namespace coro::net
{

class udp_server
{
public:
    struct options
    {
        /// The local address to bind to to recv packets from.
        net::ip_address address{net::ip_address::from_string("0.0.0.0")};
        /// The port to recv packets from.
        uint16_t port{8080};
    };

    explicit udp_server(
        io_scheduler& io_scheduler,
        options opts =
            options{
                .address = net::ip_address::from_string("0.0.0.0"),
                .port = 8080,
            }
    );

    udp_server(const udp_server&) = delete;
    udp_server(udp_server&&) = default;
    auto operator=(const udp_server&) -> udp_server& = delete;
    auto operator=(udp_server&&) -> udp_server& = default;
    ~udp_server() = default;

    auto recvfrom(
        std::span<char>& buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<std::optional<udp_client::options>>;

private:
    io_scheduler& m_io_scheduler;
    options m_options;
    net::socket m_accept_socket{-1};
};

} // namespace coro::net

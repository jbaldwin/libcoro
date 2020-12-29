#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/net/hostname.hpp"
#include "coro/net/socket.hpp"
#include "coro/connect.hpp"
#include "coro/poll.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <optional>
#include <variant>

namespace coro
{
class io_scheduler;

class tcp_client
{
public:
    struct options
    {
        std::variant<net::hostname, net::ip_address> address{net::ip_address::from_string("127.0.0.1")};
        int16_t       port{8080};
        net::domain_t domain{net::domain_t::ipv4};
    };

    tcp_client(io_scheduler& scheduler, options opts = options{{net::ip_address::from_string("127.0.0.1")}, 8080, net::domain_t::ipv4});
    tcp_client(const tcp_client&) = delete;
    tcp_client(tcp_client&&)      = default;
    auto operator=(const tcp_client&) noexcept -> tcp_client& = delete;
    auto operator=(tcp_client&&) noexcept -> tcp_client& = default;
    ~tcp_client()                                        = default;

    auto connect(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<connect_status>;

    auto socket() const -> const net::socket& { return m_socket; }
    auto socket() -> net::socket& { return m_socket; }

private:
    /// The scheduler that will drive this tcp client.
    io_scheduler& m_io_scheduler;
    /// Options for what server to connect to.
    options m_options;
    /// The tcp socket.
    net::socket m_socket;
    /// Cache the status of the connect in the event the user calls connect() again.
    std::optional<connect_status> m_connect_status{std::nullopt};
};

} // namespace coro

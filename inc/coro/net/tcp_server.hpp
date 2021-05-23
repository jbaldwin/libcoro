#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/tcp_client.hpp"
#include "coro/task.hpp"

#include <fcntl.h>
#include <sys/socket.h>

namespace coro
{
class io_scheduler;
} // namespace coro

namespace coro::net
{
class ssl_context;

class tcp_server
{
public:
    struct options
    {
        /// The ip address for the tcp server to bind and listen on.
        net::ip_address address{net::ip_address::from_string("0.0.0.0")};
        /// The port for the tcp server to bind and listen on.
        uint16_t port{8080};
        /// The kernel backlog of connections to buffer.
        int32_t backlog{128};
        /// Should this tcp server use TLS/SSL?  If provided all accepted connections will use the
        /// given SSL certificate and private key to secure the connections.
        ssl_context* ssl_ctx{nullptr};
    };

    tcp_server(
        std::shared_ptr<io_scheduler> scheduler,
        options                       opts = options{
            .address = net::ip_address::from_string("0.0.0.0"), .port = 8080, .backlog = 128, .ssl_ctx = nullptr});

    tcp_server(const tcp_server&) = delete;
    tcp_server(tcp_server&& other);
    auto operator=(const tcp_server&) -> tcp_server& = delete;
    auto operator                                    =(tcp_server&& other) -> tcp_server&;
    ~tcp_server()                                    = default;

    /**
     * Polls for new incoming tcp connections.
     * @param timeout How long to wait for a new connection before timing out, zero waits indefinitely.
     * @return The result of the poll, 'event' means the poll was successful and there is at least 1
     *         connection ready to be accepted.
     */
    auto poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<coro::poll_status>
    {
        return m_io_scheduler->poll(m_accept_socket, coro::poll_op::read, timeout);
    }

    /**
     * Accepts an incoming tcp client connection.  On failure the tcp clients socket will be set to
     * and invalid state, use the socket.is_value() to verify the client was correctly accepted.
     * @return The newly connected tcp client connection.
     */
    auto accept() -> coro::net::tcp_client;

private:
    /// The io scheduler for awaiting new connections.
    std::shared_ptr<io_scheduler> m_io_scheduler{nullptr};
    /// The bind and listen options for this server.
    options m_options;
    /// The socket for accepting new tcp connections on.
    net::socket m_accept_socket{-1};
};

} // namespace coro::net

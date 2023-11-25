#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/tcp/client.hpp"
#include "coro/task.hpp"

#include <fcntl.h>
#include <sys/socket.h>

namespace coro
{
class io_scheduler;
} // namespace coro

namespace coro::net::tcp
{

class server
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
    };

    explicit server(
        std::shared_ptr<io_scheduler> scheduler,
        options                       opts = options{
                                  .address = net::ip_address::from_string("0.0.0.0"),
                                  .port    = 8080,
                                  .backlog = 128,
        });

    server(const server&) = delete;
    server(server&& other);
    auto operator=(const server&) -> server& = delete;
    auto operator=(server&& other) -> server&;
    ~server() = default;

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
     * Accepts an incoming tcp client connection.  On failure the tls clients socket will be set to
     * and invalid state, use the socket.is_value() to verify the client was correctly accepted.
     * @return The newly connected tcp client connection.
     */
    auto accept() -> coro::net::tcp::client;

private:
    friend client;
    /// The io scheduler for awaiting new connections.
    std::shared_ptr<io_scheduler> m_io_scheduler{nullptr};
    /// The bind and listen options for this server.
    options m_options;
    /// The socket for accepting new tcp connections on.
    net::socket m_accept_socket{-1};
};

} // namespace coro::net::tcp

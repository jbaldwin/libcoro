#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/tcp/client.hpp"
#include "coro/task.hpp"

#include <fcntl.h>
#include <sys/socket.h>

namespace coro
{

class scheduler;
} // namespace coro

namespace coro::net::tcp
{

class server
{
public:
    struct options
    {
        /// The kernel backlog of connections to buffer.
        int32_t backlog{128};
    };

    explicit server(
        std::unique_ptr<coro::scheduler>& scheduler,
        const net::socket_address&        endpoint,
        options                           opts = options{

            .backlog = 128,
        });

    server(const server&) = delete;
    server(server&& other);
    auto operator=(const server&) -> server& = delete;
    auto operator=(server&& other) -> server&;
    ~server() = default;

    /**
     * Asynchronously waits for an incoming TCP connection and accepts it.
     *
     * @param timeout How long to wait for a new connection before timing out, zero waits indefinitely.
     * @return The newly connected tcp client connection on success or an io_status describing the failure.
     */
    auto accept(std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<expected<coro::net::tcp::client, io_status>>
    {
        // Fast path
        if (m_is_read_ready)
        {
            auto client = accept_now();
            if (!client && client.error().try_again())
            {
                // Failed to read, marking as unready and goint to poll
                m_is_read_ready = false;
            }
            else
            {
                // Operation was successful (error is a success too)
                co_return client;
            }
        }

        // Waiting for readiness
        auto pstatus = co_await poll(timeout);
        if (pstatus != coro::poll_status::read)
        {
            co_return unexpected<io_status>{make_io_status_from_poll_status(pstatus)};
        }
        m_is_read_ready = true;

        co_return accept_now();
    };

    /**
     * @return The tcp accept socket this server is using.
     * @{
     **/
    [[nodiscard]] auto accept_socket() -> net::socket& { return m_accept_socket; }
    [[nodiscard]] auto accept_socket() const -> const net::socket& { return m_accept_socket; }
    /** @} */

    auto shutdown()
    {
        m_cancel_trigger.signal_stop();
        m_accept_socket.shutdown(coro::poll_op::read_write);
    }

private:
    /**
     * Polls for new incoming tcp connections.
     * @param timeout How long to wait for a new connection before timing out, zero waits indefinitely.
     * @return The result of the poll, 'event' means the poll was successful and there is at least 1
     *         connection ready to be accepted.
     */
    auto poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<coro::poll_status>
    {
        return m_scheduler->poll(m_accept_socket, coro::poll_op::read, timeout, m_cancel_trigger.get_token());
    }

    /**
     * Accepts an incoming tcp client connection.
     * @return The newly connected tcp client connection.
     */
    auto accept_now() -> expected<coro::net::tcp::client, io_status>
    {
        auto client_endpoint     = socket_address::make_uninitialised();
        auto [sockaddr, socklen] = client_endpoint.native_mutable_data();

        int sock_fd = ::accept(m_accept_socket.native_handle(), sockaddr, socklen);
        if (sock_fd == -1)
        {
            return unexpected<io_status>{make_io_status_from_native(errno)};
        }

        return tcp::client{m_scheduler, net::socket{sock_fd}, client_endpoint};
    };

private:
    friend client;
    /// The io scheduler for awaiting new connections.
    coro::scheduler* m_scheduler{nullptr};
    /// The bind and listen options for this server.
    options m_options;
    /// The socket for accepting new tcp connections on.
    net::socket m_accept_socket{-1};
    /// Stop signal to trigger a cancellation of the async accept poll operation.
    poll_stop_source m_cancel_trigger{};

    /**
     * Readiness flags for epoll Edge-Triggered (ET) mode.
     * In ET mode, notifications are only sent when the descriptor state changes.
     * These flags cache the readiness state to avoid unnecessary poll() calls.
     */

    /// True if the socket might have connections to accept.
    /// Must be set to true after polling.
    /// Must be set to false after accept() returns EAGAIN/EWOULDBLOCK.
    /// false by default, because the socket usually has no connections to accept creation
    bool m_is_read_ready{false};
};

} // namespace coro::net::tcp

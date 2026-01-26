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
        const net::socket_address&endpoint,
        options                              opts = options{

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
     * Use the socket.is_valid() to verify the client was correctly accepted.
     *
     * @param timeout How long to wait for a new connection before timing out, zero waits indefinitely.
     * @return The newly connected tcp client connection on success or an io_status describing the failure.
     */
    auto accept(std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<expected<coro::net::tcp::client, io_status>>
    {
        auto pstatus = co_await poll(timeout);

        if (pstatus != coro::poll_status::read)
        {
            co_return unexpected{make_io_status_from_poll_status(pstatus)};
        }

        auto client = accept_now();
        if (!client.socket().is_valid()) {
            co_return unexpected{make_io_status_from_native(errno)};
        }
        co_return client;
    };

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
     * Accepts an incoming tcp client connection.  On failure the tls clients socket will be set to
     * and invalid state, use the socket.is_valid() to verify the client was correctly accepted.
     * @return The newly connected tcp client connection.
     */
    auto accept_now() -> coro::net::tcp::client;

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
    friend client;
    /// The io scheduler for awaiting new connections.
    coro::scheduler* m_scheduler{nullptr};
    /// The bind and listen options for this server.
    options m_options;
    /// The socket for accepting new tcp connections on.
    net::socket m_accept_socket{-1};
    /// Stop signal to trigger a cancellation of the async accept poll operation.
    poll_stop_source m_cancel_trigger{};
};

} // namespace coro::net::tcp

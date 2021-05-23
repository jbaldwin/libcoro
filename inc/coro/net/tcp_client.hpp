#pragma once

#include "coro/concepts/buffer.hpp"
#include "coro/io_scheduler.hpp"
#include "coro/net/connect.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/recv_status.hpp"
#include "coro/net/send_status.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/ssl_context.hpp"
#include "coro/net/ssl_handshake_status.hpp"
#include "coro/poll.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <memory>
#include <optional>

namespace coro::net
{
class tcp_server;

class tcp_client
{
public:
    struct options
    {
        /// The  ip address to connect to.  Use a dns_resolver to turn hostnames into ip addresses.
        net::ip_address address{net::ip_address::from_string("127.0.0.1")};
        /// The port to connect to.
        uint16_t port{8080};
        /// Should this tcp_client connect using a secure connection SSL/TLS?
        ssl_context* ssl_ctx{nullptr};
    };

    /**
     * Creates a new tcp client that can connect to an ip address + port.  By default the socket
     * created will be in non-blocking mode, meaning that any sending or receiving of data should
     * poll for event readiness prior.
     * @param scheduler The io scheduler to drive the tcp client.
     * @param opts See tcp_client::options for more information.
     */
    tcp_client(
        std::shared_ptr<io_scheduler> scheduler,
        options                       opts = options{
            .address = {net::ip_address::from_string("127.0.0.1")}, .port = 8080, .ssl_ctx = nullptr});
    tcp_client(const tcp_client&) = delete;
    tcp_client(tcp_client&& other);
    auto operator=(const tcp_client&) noexcept -> tcp_client& = delete;
    auto operator                                             =(tcp_client&& other) noexcept -> tcp_client&;
    ~tcp_client();

    /**
     * @return The tcp socket this client is using.
     * @{
     **/
    auto socket() -> net::socket& { return m_socket; }
    auto socket() const -> const net::socket& { return m_socket; }
    /** @} */

    /**
     * Connects to the address+port with the given timeout.  Once connected calling this function
     * only returns the connected status, it will not reconnect.
     * @param timeout How long to wait for the connection to establish? Timeout of zero is indefinite.
     * @return The result status of trying to connect.
     */
    auto connect(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<net::connect_status>;

    /**
     * If this client is connected and the connection is SSL/TLS then perform the ssl handshake.
     * This must be done after a successful connect() call for clients that are initiating a
     * connection to a server.  This must be done after a successful accept() call for clients that
     * have been accepted by a tcp_server.  TCP server 'client's start in the connected state and
     * thus skip the connect() call.
     *
     * tcp_client initiating to a server:
     *      tcp_client client{...options...};
     *      co_await client.connect();
     *      co_await client.ssl_handshake(); // <-- only perform if ssl/tls connection
     *
     * tcp_server accepting a client connection:
     *      tcp_server server{...options...};
     *      co_await server.poll();
     *      auto client = server.accept();
     *      if(client.socket().is_valid())
     *      {
     *          co_await client.ssl_handshake(); // <-- only perform if ssl/tls connection
     *      }
     * @param timeout How long to allow for the ssl handshake to successfully complete?
     * @return The result of the ssl handshake.
     */
    auto ssl_handshake(std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<ssl_handshake_status>;

    /**
     * Polls for the given operation on this client's tcp socket.  This should be done prior to
     * calling recv and after a send that doesn't send the entire buffer.
     * @param op The poll operation to perform, use read for incoming data and write for outgoing.
     * @param timeout The amount of time to wait for the poll event to be ready.  Use zero for infinte timeout.
     * @return The status result of th poll operation.  When poll_status::event is returned then the
     *         event operation is ready.
     */
    auto poll(coro::poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>
    {
        return m_io_scheduler->poll(m_socket, op, timeout);
    }

    /**
     * Receives incoming data into the given buffer.  By default since all tcp client sockets are set
     * to non-blocking use co_await poll() to determine when data is ready to be received.
     * @param buffer Received bytes are written into this buffer up to the buffers size.
     * @return The status of the recv call and a span of the bytes recevied (if any).  The span of
     *         bytes will be a subspan or full span of the given input buffer.
     */
    template<concepts::mutable_buffer buffer_type>
    auto recv(buffer_type&& buffer) -> std::pair<recv_status, std::span<char>>
    {
        // If the user requested zero bytes, just return.
        if (buffer.empty())
        {
            return {recv_status::ok, std::span<char>{}};
        }

        if (m_options.ssl_ctx == nullptr)
        {
            auto bytes_recv = ::recv(m_socket.native_handle(), buffer.data(), buffer.size(), 0);
            if (bytes_recv > 0)
            {
                // Ok, we've recieved some data.
                return {recv_status::ok, std::span<char>{buffer.data(), static_cast<size_t>(bytes_recv)}};
            }
            else if (bytes_recv == 0)
            {
                // On TCP stream sockets 0 indicates the connection has been closed by the peer.
                return {recv_status::closed, std::span<char>{}};
            }
            else
            {
                // Report the error to the user.
                return {static_cast<recv_status>(errno), std::span<char>{}};
            }
        }
        else
        {
            ERR_clear_error();
            size_t bytes_recv{0};
            int    r = SSL_read_ex(m_ssl_info.m_ssl_ptr.get(), buffer.data(), buffer.size(), &bytes_recv);
            if (r == 0)
            {
                int err = SSL_get_error(m_ssl_info.m_ssl_ptr.get(), r);
                if (err == SSL_ERROR_WANT_READ)
                {
                    return {recv_status::would_block, std::span<char>{}};
                }
                else
                {
                    // TODO: Flesh out all possible ssl errors:
                    // https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html
                    return {recv_status::ssl_error, std::span<char>{}};
                }
            }
            else
            {
                return {recv_status::ok, std::span<char>{buffer.data(), static_cast<size_t>(bytes_recv)}};
            }
        }
    }

    /**
     * Sends outgoing data from the given buffer.  If a partial write occurs then use co_await poll()
     * to determine when the tcp client socket is ready to be written to again.  On partial writes
     * the status will be 'ok' and the span returned will be non-empty, it will contain the buffer
     * span data that was not written to the client's socket.
     * @param buffer The data to write on the tcp socket.
     * @return The status of the send call and a span of any remaining bytes not sent.  If all bytes
     *         were successfully sent the status will be 'ok' and the remaining span will be empty.
     */
    template<concepts::const_buffer buffer_type>
    auto send(const buffer_type& buffer) -> std::pair<send_status, std::span<const char>>
    {
        // If the user requested zero bytes, just return.
        if (buffer.empty())
        {
            return {send_status::ok, std::span<const char>{buffer.data(), buffer.size()}};
        }

        if (m_options.ssl_ctx == nullptr)
        {
            auto bytes_sent = ::send(m_socket.native_handle(), buffer.data(), buffer.size(), 0);
            if (bytes_sent >= 0)
            {
                // Some or all of the bytes were written.
                return {send_status::ok, std::span<const char>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
            }
            else
            {
                // Due to the error none of the bytes were written.
                return {static_cast<send_status>(errno), std::span<const char>{buffer.data(), buffer.size()}};
            }
        }
        else
        {
            ERR_clear_error();
            size_t bytes_sent{0};
            int    r = SSL_write_ex(m_ssl_info.m_ssl_ptr.get(), buffer.data(), buffer.size(), &bytes_sent);
            if (r == 0)
            {
                int err = SSL_get_error(m_ssl_info.m_ssl_ptr.get(), r);
                if (err == SSL_ERROR_WANT_WRITE)
                {
                    return {send_status::would_block, std::span<char>{}};
                }
                else
                {
                    // TODO: Flesh out all possible ssl errors:
                    // https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html
                    return {send_status::ssl_error, std::span<char>{}};
                }
            }
            else
            {
                return {send_status::ok, std::span<const char>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
            }
        }
    }

private:
    struct ssl_deleter
    {
        auto operator()(SSL* ssl) const -> void { SSL_free(ssl); }
    };

    using ssl_unique_ptr = std::unique_ptr<SSL, ssl_deleter>;

    enum class ssl_connection_type
    {
        /// This connection is a client connecting to a server.
        connect,
        /// This connection is an accepted connection on a sever.
        accept
    };

    struct ssl_info
    {
        ssl_info() {}
        explicit ssl_info(ssl_connection_type type) : m_ssl_connection_type(type) {}
        ssl_info(const ssl_info&) noexcept = delete;
        ssl_info(ssl_info&& other) noexcept
            : m_ssl_connection_type(std::exchange(other.m_ssl_connection_type, ssl_connection_type::connect)),
              m_ssl_ptr(std::move(other.m_ssl_ptr)),
              m_ssl_error(std::exchange(other.m_ssl_error, false)),
              m_ssl_handshake_status(std::move(other.m_ssl_handshake_status))
        {
        }

        auto operator=(const ssl_info&) noexcept -> ssl_info& = delete;

        auto operator=(ssl_info&& other) noexcept -> ssl_info&
        {
            if (std::addressof(other) != this)
            {
                m_ssl_connection_type  = std::exchange(other.m_ssl_connection_type, ssl_connection_type::connect);
                m_ssl_ptr              = std::move(other.m_ssl_ptr);
                m_ssl_error            = std::exchange(other.m_ssl_error, false);
                m_ssl_handshake_status = std::move(other.m_ssl_handshake_status);
            }
            return *this;
        }

        /// What kind of connection is this, client initiated connect or server side accept?
        ssl_connection_type m_ssl_connection_type{ssl_connection_type::connect};
        /// OpenSSL ssl connection.
        ssl_unique_ptr m_ssl_ptr{nullptr};
        /// Was there an error with the SSL/TLS connection?
        bool m_ssl_error{false};
        /// The result of the ssl handshake.
        std::optional<ssl_handshake_status> m_ssl_handshake_status{std::nullopt};
    };

    /// The tcp_server creates already connected clients and provides a tcp socket pre-built.
    friend tcp_server;
    tcp_client(std::shared_ptr<io_scheduler> scheduler, net::socket socket, options opts);

    /// The scheduler that will drive this tcp client.
    std::shared_ptr<io_scheduler> m_io_scheduler{nullptr};
    /// Options for what server to connect to.
    options m_options{};
    /// The tcp socket.
    net::socket m_socket{-1};
    /// Cache the status of the connect in the event the user calls connect() again.
    std::optional<net::connect_status> m_connect_status{std::nullopt};
    /// SSL/TLS specific information if m_options.ssl_ctx != nullptr.
    ssl_info m_ssl_info{};

    static auto ssl_shutdown_and_free(
        std::shared_ptr<io_scheduler> io_scheduler,
        net::socket                   s,
        ssl_unique_ptr                ssl_ptr,
        std::chrono::milliseconds     timeout = std::chrono::milliseconds{0}) -> coro::task<void>;
};

} // namespace coro::net

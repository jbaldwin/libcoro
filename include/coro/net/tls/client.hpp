#ifdef LIBCORO_FEATURE_TLS

    #pragma once

    #include "coro/concepts/buffer.hpp"
    #include "coro/io_scheduler.hpp"
    #include "coro/net/connect.hpp"
    #include "coro/net/ip_address.hpp"
    #include "coro/net/socket.hpp"
    #include "coro/net/tls/connection_status.hpp"
    #include "coro/net/tls/context.hpp"
    #include "coro/net/tls/recv_status.hpp"
    #include "coro/net/tls/send_status.hpp"
    #include "coro/poll.hpp"
    #include "coro/task.hpp"

    #include <chrono>
    #include <memory>
    #include <optional>

namespace coro::net::tls
{
class server;

class client
{
public:
    struct options
    {
        /// The ip address to connect to.  Use a dns::resolver to turn hostnames into ip addresses.
        net::ip_address address{net::ip_address::from_string("127.0.0.1")};
        /// The port to connect to.
        uint16_t port{8080};
    };

    /**
     * Creates a new tls client that can connect to an ip address + port.  By default the socket
     * created will be in non-blocking mode, meaning that any sending or receiving of data should
     * be polled for event readiness prior.
     * @param scheduler The io scheduler to drive the tls client.
     * @param tls_ctx The tls context.
     * @param opts See tls::client::options for more information.
     */
    explicit client(
        std::shared_ptr<io_scheduler> scheduler,
        std::shared_ptr<context>      tls_ctx,
        options                       opts = options{
                                  .address = {net::ip_address::from_string("127.0.0.1")},
                                  .port    = 8080,
        });
    client(const client&) = delete;
    client(client&& other);
    auto operator=(const client&) noexcept -> client& = delete;
    auto operator=(client&& other) noexcept -> client&;
    ~client();

    /**
     * @return The tcp socket this client is using.
     * @{
     **/
    auto socket() -> net::socket& { return m_socket; }
    auto socket() const -> const net::socket& { return m_socket; }
    /** @} */

    /**
     * Connects to the address+port with the given timeout and completes the tls handshake.
     * Once connected calling this function only returns the connected status, it will not reconnect.
     * @param timeout How long to wait for the connection to establish? Timeout of zero is indefinite.
     * @return The result status of trying to connect.
     */
    auto connect(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<connection_status>;

    /**
     * Polls for the given operation on this client's socket.  This should be done prior to
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
            return {recv_status::buffer_is_empty, std::span<char>{}};
        }

        auto* tls = m_tls_info.m_tls_ptr.get();

        ERR_clear_error();
        size_t bytes_recv{0};
        int    r = SSL_read_ex(tls, buffer.data(), buffer.size(), &bytes_recv);
        if (r == 0)
        {
            // https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html
            int err = SSL_get_error(tls, r);
            switch (err)
            {
                case SSL_ERROR_NONE:
                    if (bytes_recv > 0)
                    {
                        return {recv_status::ok, std::span<char>{buffer.data(), static_cast<size_t>(bytes_recv)}};
                    }
                    else
                    {
                        return {recv_status::want_read, std::span<char>{}};
                    }
                case SSL_ERROR_WANT_READ:
                    if (bytes_recv > 0)
                    {
                        return {recv_status::ok, std::span<char>{buffer.data(), static_cast<size_t>(bytes_recv)}};
                    }
                    else
                    {
                        return {recv_status::want_read, std::span<char>{}};
                    }
                default:
                    return {static_cast<recv_status>(err), std::span<char>{}};
            }
        }
        else
        {
            return {recv_status::ok, std::span<char>{buffer.data(), static_cast<size_t>(bytes_recv)}};
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

        auto* tls = m_tls_info.m_tls_ptr.get();

        ERR_clear_error();
        size_t bytes_sent{0};
        int    r = SSL_write_ex(tls, buffer.data(), buffer.size(), &bytes_sent);
        if (r == 0)
        {
            // https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html
            int err = SSL_get_error(tls, r);
            return {static_cast<send_status>(err), std::span<char>{}};
        }
        else
        {
            return {send_status::ok, std::span<const char>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
        }
    }

private:
    /**
     * @param timeout How long to allow for the tls handshake to successfully complete?
     * @return The result of the tls handshake.
     */
    auto handshake(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<connection_status>;

    struct tls_deleter
    {
        auto operator()(SSL* ssl) const -> void { SSL_free(ssl); }
    };

    using tls_unique_ptr = std::unique_ptr<SSL, tls_deleter>;

    enum class tls_connection_type
    {
        /// This connection is a client connecting to a server.
        connect,
        /// This connection is an accepted connection on a sever.
        accept
    };

    struct tls_info
    {
        tls_info() {}
        explicit tls_info(tls_connection_type type) : m_tls_connection_type(type) {}
        tls_info(const tls_info&) noexcept = delete;
        tls_info(tls_info&& other) noexcept
            : m_tls_connection_type(std::exchange(other.m_tls_connection_type, tls_connection_type::connect)),
              m_tls_ptr(std::move(other.m_tls_ptr)),
              m_tls_error(std::exchange(other.m_tls_error, false)),
              m_tls_connection_status(std::move(other.m_tls_connection_status))
        {
        }

        auto operator=(const tls_info&) noexcept -> tls_info& = delete;

        auto operator=(tls_info&& other) noexcept -> tls_info&
        {
            if (std::addressof(other) != this)
            {
                m_tls_connection_type   = std::exchange(other.m_tls_connection_type, tls_connection_type::connect);
                m_tls_ptr               = std::move(other.m_tls_ptr);
                m_tls_error             = std::exchange(other.m_tls_error, false);
                m_tls_connection_status = std::move(other.m_tls_connection_status);
            }
            return *this;
        }

        /// What kind of connection is this, client initiated connect or server side accept?
        tls_connection_type m_tls_connection_type{tls_connection_type::connect};
        /// OpenSSL ssl connection.
        tls_unique_ptr m_tls_ptr{nullptr};
        /// Was there an error with the SSL/TLS connection?
        bool m_tls_error{false};
        /// The result of the tls connection and handshake.
        std::optional<connection_status> m_tls_connection_status{std::nullopt};
    };

    /// The tls::server creates already connected clients and provides a tcp socket pre-built.
    friend server;
    client(std::shared_ptr<io_scheduler> scheduler, std::shared_ptr<context> tls_ctx, net::socket socket, options opts);

    /// The scheduler that will drive this tcp client.
    std::shared_ptr<io_scheduler> m_io_scheduler{nullptr};
    // The tls context.
    std::shared_ptr<context> m_tls_ctx{nullptr};
    /// Options for what server to connect to.
    options m_options{};
    /// The tcp socket.
    net::socket m_socket{-1};
    /// Cache the status of the connect in the event the user calls connect() again.
    std::optional<connection_status> m_connect_status{std::nullopt};
    /// SSL/TLS specific information.
    tls_info m_tls_info{};

    static auto tls_shutdown_and_free(
        std::shared_ptr<io_scheduler> io_scheduler,
        net::socket                   s,
        tls_unique_ptr                tls_ptr,
        std::chrono::milliseconds     timeout = std::chrono::milliseconds{0}) -> coro::task<void>;
};

} // namespace coro::net::tls

#endif // #ifdef LIBCORO_FEATURE_TLS

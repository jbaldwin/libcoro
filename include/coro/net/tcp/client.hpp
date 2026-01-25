#pragma once

#include "coro/concepts/buffer.hpp"
#include "coro/net/connect.hpp"
#include "coro/net/io_status.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/socket_address.hpp"
#include "coro/poll.hpp"
#include "coro/scheduler.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <memory>
#include <optional>

namespace coro::net::tcp
{
class server;

class client
{
public:
    /**
     * Creates a new tcp client that can connect to an ip address + port. By default, the socket
     * created will be in non-blocking mode, meaning that any sending or receiving of data should
     * poll for event readiness prior.
     * @param scheduler The io scheduler to drive the tcp client.
     * @param opts See client::options for more information.
     */
    explicit client(std::unique_ptr<coro::scheduler>& scheduler, net::socket_address endpoint);
    client(const client& other);
    client(client&& other) noexcept;
    auto operator=(const client& other) noexcept -> client&;
    auto operator=(client&& other) noexcept -> client&;
    ~client();

    /**
     * @return The tcp socket this client is using.
     * @{
     **/
    [[nodiscard]] auto socket() -> net::socket& { return m_socket; }
    [[nodiscard]] auto socket() const -> const net::socket& { return m_socket; }
    /** @} */

    /**
     * Connects to the address+port with the given timeout.  Once connected calling this function
     * only returns the connected status, it will not reconnect.
     * @param timeout How long to wait for the connection to establish? Timeout of zero is indefinite.
     * @return The result status of trying to connect.
     */
    auto connect(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<net::connect_status>;

    /**
     * Attempts to asynchronously read data from the socket into the provided buffer.
     *
     * The function may read fewer bytes than requested. In that case, the returned
     * status will be 'ok' and the returned span will reference the prefix of the
     * buffer that was filled with received data.
     *
     * @see read_exact()
     * @param buffer Destination buffer to read data into.
     * @param timeout Maximum time to wait for the socket to become readable
     *                Use 0 for infinite timeout.
     * @return A pair of:
     *          - status of the operation
     *          - span pointing to the read part of buffer
     * @{
     */
    auto read_some(std::span<std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<std::byte>>>
    {
        auto poll_status = co_await poll(poll_op::read, timeout);
        if (poll_status != poll_status::read)
        {
            co_return std::pair{make_io_status_poll_status(poll_status), std::span<std::byte>{}};
        }
        co_return recv(buffer);
    }
    template<concepts::mutable_buffer buffer_type>
    auto read_some(buffer_type&& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<std::byte>>>
    {
        return read_some(std::as_writable_bytes(std::span{buffer}), timeout);
    }
    /** }@ */

    /**
     * Asynchronously reads exactly buffer.size() bytes from the socket.
     *
     * Repeatedly calls write_some until either:
     * - buffer.size() bytes have been read, or
     * - an error or timeout occurs.
     *
     * @see read_some()
     * @param buffer Destination buffer to read data into.
     * @param timeout Maximum time to wait for the socket to become readable
     *                Use 0 for infinite timeout.
     * @return A pair of:
     *          - status of the operation
     *          - span pointing to the read part of buffer
     * @{
     */
    auto read_exact(std::span<std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<std::byte>>>
    {
        const auto           start_time = std::chrono::steady_clock::now();
        std::span<std::byte> remaining  = buffer;

        while (!remaining.empty())
        {
            std::chrono::milliseconds remaining_timeout{0};
            if (timeout.count() > 0)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time);

                if (elapsed >= timeout)
                {
                    // Returning read prefix of the span
                    co_return {
                        io_status{io_status::kind::timeout}, buffer.subspan(0, buffer.size() - remaining.size())};
                }
                remaining_timeout = timeout - elapsed;
            }

            auto [status, read_span] = co_await read_some(remaining, remaining_timeout);
            remaining                = remaining.subspan(read_span.size());

            if (!status.is_ok())
            {
                co_return {status, buffer.subspan(0, buffer.size() - remaining.size())};
            }
        }

        co_return {io_status{io_status::kind::ok}, buffer};
    }

    template<concepts::mutable_buffer buffer_type>
    auto read_exact(buffer_type&& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<std::byte>>>
    {
        return read_exact(std::as_writable_bytes(std::span{buffer}), timeout);
    }
    /** }@ */

    /**
     * Attempts to asynchronously write data from the provided buffer to the socket.
     *
     * If only part of the buffer is written, the returned status will be 'ok' and
     * the returned span will reference the portion of the original buffer that
     * was not sent.
     *
     * @see write_all()
     * @param buffer Buffer containing the data to write.
     * @param timeout Maximum time to wait for the socket to become writable.
     *                Use 0 for infinite timeout.
     * @return A pair of:
     *          - status of the operation
     *          - span pointing to the unsent part of the buffer
     * @{
     */
    auto write_some(
        std::span<const std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        auto poll_status = co_await poll(poll_op::write, timeout);
        if (poll_status != poll_status::write)
        {
            co_return std::pair{make_io_status_poll_status(poll_status), buffer};
        }
        co_return send(buffer);
    }
    template<concepts::const_buffer buffer_type>
    auto write_some(const buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        return write_some(std::as_bytes(std::span{buffer}), timeout);
    }
    /** }@ */

    /**
     * Asynchronously writes the entire contents of the provided buffer to the socket.
     * Repeatedly call write_some until either:
     * - all bytes have been sent, or
     * - an error or timeout occurs.
     *
     * @see write_some()
     * @param buffer The data to write on the tcp socket.
     * @return A pair of:
     *          - status of the operation
     *          - span pointing to the unsent part of the buffer;
     *            empty if all data was sent successfully
     * @{
     */
    auto write_all(
        std::span<const std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        const auto                 start_time = std::chrono::steady_clock::now();
        std::span<const std::byte> remaining  = buffer;

        // Trying to send something without polling
        //        {
        //            auto [status, unsent] = send(buffer);
        //            remaining = unsent;
        //
        //            if (!status.is_ok() && status.type != io_status::kind::try_again)
        //            {
        //                co_return {status, remaining};
        //            }
        //        }

        while (!remaining.empty())
        {
            std::chrono::milliseconds remaining_timeout{0};
            if (timeout.count() > 0)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time);

                if (elapsed >= timeout)
                {
                    co_return {io_status{io_status::kind::timeout}, remaining};
                }
                remaining_timeout = timeout - elapsed;
            }

            auto [status, unsent_span] = co_await write_some(remaining, remaining_timeout);
            remaining                  = unsent_span;

            if (!status.is_ok())
            {
                co_return {status, remaining};
            }
        }

        co_return {io_status{io_status::kind::ok}, {}};
    }
    template<concepts::const_buffer buffer_type>
    auto write_all(const buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        return write_all(std::as_bytes(std::span{buffer}), timeout);
    }
    /** }@ */

    /**
     * Polls for the given operation on this client's tcp socket.  This should be done prior to
     * calling recv and after a send call that doesn't send the entire buffer.
     * @param op The poll operation to perform, use read for incoming data and write for outgoing.
     * @param timeout The amount of time to wait for the poll event to be ready.  Use zero for infinte timeout.
     * @return The status result of th poll operation.  When poll_status::read or poll_status::write is returned then
     *         this specific event operation is ready.
     */
    auto poll(const coro::poll_op op, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>
    {
        return m_scheduler->poll(m_socket, op, timeout);
    }

    /**
     * Receives incoming data into the given buffer. By default, since all tcp client sockets are set
     * to non-blocking use co_await poll() to determine when data is ready to be received.
     * @param buffer Received bytes are written into this buffer up to the buffers size.
     * @return The status of the recv call and a span of the bytes received (if any). The span of
     *         bytes will be a subspan or full span of the given input buffer.
     */
    template<
        concepts::mutable_buffer buffer_type,
        typename element_type = typename concepts::mutable_buffer_traits<buffer_type>::element_type>
    auto recv(buffer_type&& buffer) -> std::pair<io_status, std::span<element_type>>
    {
        // If the user requested zero bytes, just return.
        if (buffer.empty())
        {
            return {io_status{io_status::kind::ok}, std::span<element_type>{}};
        }

        auto bytes_recv = ::recv(m_socket.native_handle(), buffer.data(), buffer.size(), 0);
        if (bytes_recv > 0)
        {
            // Ok, we've received some data.
            return {
                io_status{io_status::kind::ok},
                std::span<element_type>{buffer.data(), static_cast<size_t>(bytes_recv)}};
        }

        if (bytes_recv == 0)
        {
            // On TCP stream sockets 0 indicates the connection has been closed by the peer.
            return {io_status{io_status::kind::closed}, std::span<element_type>{}};
        }

        // Report the error to the user.
        return {make_io_status_from_native(errno), std::span<element_type>{}};
    }

    /**
     * Sends outgoing data from the given buffer. If a partial write occurs then use co_await poll()
     * to determine when the tcp client socket is ready to be written to again.  On partial writes
     * the status will be 'ok' and the span returned will be non-empty, it will contain the buffer
     * span data that was not written to the client's socket.
     * @param buffer The data to write on the tcp socket.
     * @return The status of the send call and a span of any remaining bytes not sent.  If all bytes
     *         were successfully sent the status will be 'ok' and the remaining span will be empty.
     */
    template<
        concepts::const_buffer buffer_type,
        typename element_type = typename concepts::const_buffer_traits<buffer_type>::element_type>
    auto send(const buffer_type& buffer) -> std::pair<io_status, std::span<element_type>>
    {
        // If the user requested zero bytes, just return.
        if (buffer.empty())
        {
            return {io_status{io_status::kind::ok}, std::span<element_type>{buffer.data(), buffer.size()}};
        }

        auto bytes_sent = ::send(m_socket.native_handle(), buffer.data(), buffer.size(), 0);
        if (bytes_sent >= 0)
        {
            // Some or all of the bytes were written.
            return {
                io_status{io_status::kind::ok},
                std::span<element_type>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
        }

        // Due to the error none of the bytes were written.
        return {make_io_status_from_native(errno), std::span<element_type>{buffer.data(), buffer.size()}};
    }

private:
    /// The tcp::server creates already connected clients and provides a tcp socket pre-built.
    friend server;
    client(coro::scheduler* scheduler, net::socket socket, const net::socket_address& endpoint);

    /// The scheduler that will drive this tcp client.
    coro::scheduler* m_scheduler{nullptr};
    /// Options for what server to connect to.
    socket_address m_endpoint;
    /// The tcp socket.
    net::socket m_socket{-1};
    /// Cache the status of the connect call in the event the user calls connect() again.
    std::optional<net::connect_status> m_connect_status{std::nullopt};
};

} // namespace coro::net::tcp

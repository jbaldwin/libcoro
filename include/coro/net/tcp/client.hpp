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
     * Creates a new tcp client that can connect to an ip address + port.
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
     * Connects to the address+port with the given timeout. Once connected calling this function
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
     * @note
     *   This function is not safe to call concurrently from multiple coroutines
     *   on the same socket.
     *
     * @see read_exact()
     *
     * @param buffer Destination buffer to read data into.
     * @param timeout Maximum time to wait for the socket to become readable
     *                Use 0 for infinite timeout.
     * @return A pair of:
     *          - status of the operation
     *          - span pointing to the read part of buffer
     */
    template<
        concepts::mutable_buffer buffer_type,
        typename element_type = typename concepts::mutable_buffer_traits<buffer_type>::element_type>
    auto read_some(buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<element_type>>>
    {
        if (buffer.empty())
        {
            co_return {io_status{io_status::kind::ok}, {}};
        }
        auto [status, buf] = co_await read_some_impl(std::as_writable_bytes(std::span{buffer}), timeout);
        co_return {status, std::span<element_type>{reinterpret_cast<element_type*>(buf.data()), buf.size()}};
    }

    /**
     * Asynchronously reads data from the socket into the provided buffer.
     *
     * This function repeatedly invokes read_some() until either:
     * - the whole buffer is filled, or
     * - an error or timeout occurs.
     *
     * The timeout is treated as a soft overall deadline for the entire operation.
     *
     * @note
     *   This function is not safe to call concurrently from multiple coroutines
     *   on the same socket.
     *
     * @see read_some()
     *
     * @param buffer
     *        Destination buffer to read data into.
     * @param timeout
     *        Maximum total time allowed for the operation.
     *        A value of 0 results in infinite timeout.
     * @return A pair of:
     *         - status of the operation
     *         - span pointing to the read part of buffer
     */
    template<
        concepts::mutable_buffer buffer_type,
        typename element_type = typename concepts::mutable_buffer_traits<buffer_type>::element_type>
    auto read_exact(buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<element_type>>>
    {
        if (buffer.empty())
        {
            co_return {io_status{io_status::kind::ok}, {}};
        }
        auto [status, buf] = co_await read_exact_impl(std::as_writable_bytes(std::span{buffer}), timeout);
        co_return {status, std::span<element_type>{reinterpret_cast<element_type*>(buf.data()), buf.size()}};
    }

    /**
     * Attempts to asynchronously write data from the provided buffer to the socket.
     *
     * The operation may write only a portion of the provided buffer.
     * In that case, the returned status will be 'ok' and the returned span
     * will reference the portion of the original buffer that was not sent.
     *
     * This function performs at most one write attempt.
     *
     * @note
     * - A successful readiness notification does not guarantee that the write
     *   will succeed; partial writes and retry conditions are possible.
     * - The returned span always refers to the original buffer.
     * - This function is not safe to call concurrently from multiple coroutines
     *   on the same socket.
     *
     * @see write_all()
     * @param buffer
     *        Buffer containing the data to write.
     * @param timeout
     *        Maximum time to wait for the socket to become writable.
     *        A value of 0 results in infinite timeout.
     * @return A pair of:
     *         - status of the operation
     *         - span pointing to the unsent portion of the buffer;
     *           empty if all data was sent successfully
     */
    template<
        concepts::const_buffer buffer_type,
        typename element_type = typename concepts::const_buffer_traits<buffer_type>::element_type>
    auto write_some(const buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<element_type>>>
    {
        static_assert(sizeof(element_type) == 1);

        if (buffer.empty())
        {
            co_return {io_status{io_status::kind::ok}, {}};
        }
        auto [status, buf] = co_await write_some_impl(std::as_bytes(std::span{buffer}), timeout);
        co_return {status, std::span<element_type>{reinterpret_cast<element_type*>(buf.data()), buf.size()}};
    }

    /**
     * Asynchronously writes the entire contents of the provided buffer to the socket.
     *
     * This function repeatedly invokes write_some() until either:
     * - all bytes have been sent successfully, or
     * - an error or timeout occurs.
     *
     * The timeout is treated as a soft overall deadline for the entire operation.
     *
     * @note
     *   This function is not safe to call concurrently from multiple coroutines
     *   on the same socket.
     *
     * @see write_some()
     *
     * @param buffer
     *        The data to write to the socket.
     * @param timeout
     *        Maximum total time allowed for the operation.
     *        A value of 0 results in infinite timeout.
     * @return A pair of:
     *         - status of the operation
     *         - span pointing to the unsent portion of the buffer;
     *           empty if all data was sent successfully
     */
    template<
        concepts::const_buffer buffer_type,
        typename element_type = typename concepts::const_buffer_traits<buffer_type>::element_type>
    auto write_all(const buffer_type& buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<element_type>>>
    {
        static_assert(sizeof(element_type) == 1);

        if (buffer.empty())
        {
            co_return {io_status{io_status::kind::ok}, {}};
        }
        auto [status, buf] = co_await write_all_impl(std::as_bytes(std::span{buffer}), timeout);
        co_return {status, std::span<element_type>{reinterpret_cast<element_type*>(buf.data()), buf.size()}};
    }

private:
    auto read_some_impl(
        std::span<std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<std::byte>>>
    {
        // Fast path
        if (m_is_read_ready)
        {
            auto [status, read] = recv(buffer);

            if (status.try_again())
            {
                // Failed to read, marking as unready and going to poll
                m_is_read_ready = false;
            }
            else
            {
                // Operation was successful (error is a success too)
                co_return {status, read};
            }
        }

        auto poll_status = co_await poll(poll_op::read, timeout);
        if (poll_status != poll_status::read)
        {
            co_return std::pair{make_io_status_from_poll_status(poll_status), std::span<std::byte>{}};
        }
        m_is_read_ready = true;

        co_return recv(buffer);
    }

    auto read_exact_impl(
        std::span<std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
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

            auto [status, read_span] = co_await read_some_impl(remaining, remaining_timeout);
            remaining                = remaining.subspan(read_span.size());

            if (!status.is_ok())
            {
                co_return {status, buffer.subspan(0, buffer.size() - remaining.size())};
            }
        }

        co_return {io_status{io_status::kind::ok}, buffer};
    }

    auto write_some_impl(
        std::span<const std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        // Fast path
        if (m_is_write_ready)
        {
            auto [status, unsent] = send(buffer);

            if (status.try_again())
            {
                // Failed to read, marking as unready and goint to poll
                m_is_write_ready = false;
            }
            else
            {
                // Operation was successful (error is a success too)
                co_return {status, unsent};
            }
        }

        // Waiting for readiness
        auto pstatus = co_await poll(poll_op::write, timeout);
        if (pstatus != poll_status::write)
        {
            co_return std::pair{make_io_status_from_poll_status(pstatus), buffer};
        }
        m_is_write_ready = true;

        co_return send(buffer);
    }

    auto write_all_impl(
        std::span<const std::byte> buffer, const std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<io_status, std::span<const std::byte>>>
    {
        const auto                 start_time = std::chrono::steady_clock::now();
        std::span<const std::byte> remaining  = buffer;

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

            auto [status, unsent_span] = co_await write_some_impl(remaining, remaining_timeout);
            remaining                  = unsent_span;

            if (!status.is_ok())
            {
                co_return {status, remaining};
            }
        }

        co_return {io_status{io_status::kind::ok}, {}};
    }

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

    /**
     * Readiness flags for epoll Edge-Triggered (ET) mode.
     * In ET mode, notifications are only sent when the descriptor state changes.
     * These flags cache the readiness state to avoid unnecessary poll() calls.
     */

    /// True if the socket might have data to read.
    /// Must be set to true after polling.
    /// Must be set to false after recv() returns EAGAIN/EWOULDBLOCK.
    /// false by default, because the socket is usually not ready for reading on creation
    bool m_is_read_ready{false};

    /// True if the socket send buffer can accept data.
    /// Must be set to true after polling.
    /// Must be set to false after send() returns EAGAIN/EWOULDBLOCK.
    /// true by default, because the socket is usually already ready for writing on creation
    bool m_is_write_ready{false};
};

} // namespace coro::net::tcp

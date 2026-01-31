#pragma once

#include "coro/concepts/buffer.hpp"
#include "coro/net/io_status.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/socket_address.hpp"
#include "coro/scheduler.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <span>

namespace coro::net::udp
{
class peer
{
public:
    /**
     * Creates a udp peer that can send packets but not receive them.  This udp peer will not explicitly
     * bind to a local ip+port.
     */
    explicit peer(std::unique_ptr<coro::scheduler>& scheduler, net::domain_t domain = net::domain_t::ipv4);

    /**
     * Creates a udp peer that can send and receive packets.  This peer will bind to the given ip_port.
     */
    explicit peer(std::unique_ptr<coro::scheduler>& scheduler, const net::socket_address& endpoint);

    peer(const peer&) noexcept = default;
    peer(peer&&) noexcept;
    auto operator=(const peer&) noexcept -> peer&;
    auto operator=(peer&&) noexcept -> peer&;
    ~peer() = default;

    /**
     * @return A reference to the underlying socket.
     */
    auto socket() noexcept -> net::socket& { return m_socket; }

    /**
     * @return A const reference to the underlying socket.
     */
    auto socket() const noexcept -> const net::socket& { return m_socket; }

    auto write_to(
        const socket_address&            address,
        const std::span<const std::byte> buffer,
        std::chrono::milliseconds        timeout = std::chrono::milliseconds{0}) -> coro::task<io_status>
    {
        auto pstatus = co_await poll(poll_op::write, timeout);
        if (pstatus != poll_status::write)
        {
            co_return make_io_status_from_poll_status(pstatus);
        }

        co_return sendto(address, buffer);
    }

    template<concepts::const_buffer buffer_type>
    auto write_to(
        const socket_address&     address,
        const buffer_type&        buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> coro::task<io_status>
    {
        return write_to(address, std::as_bytes(std::span{buffer}), timeout);
    }

    auto read_from(std::span<std::byte> buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::tuple<io_status, socket_address, std::span<std::byte>>>
    {
        auto pstatus = co_await poll(poll_op::read, timeout);
        if (pstatus != poll_status::read)
        {
            co_return {make_io_status_from_poll_status(pstatus), socket_address::make_uninitialised(), {}};
        }

        co_return recvfrom(buffer);
    }

    template<concepts::mutable_buffer buffer_type>
    auto read_from(buffer_type&& buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::tuple<io_status, socket_address, std::span<std::byte>>>
    {
        return read_from(std::as_writable_bytes(std::span{buffer}), timeout);
    }

    /**
     * @param op The poll operation to perform on the udp socket. Note that if this is a send call only
     *           udp socket (did not bind) then polling for read will not work.
     * @param timeout The timeout for the poll operation to be ready.
     * @return The result status of the poll operation.
     */
    auto poll(poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<coro::poll_status>
    {
        co_return co_await m_scheduler->poll(m_socket, op, timeout);
    }

    /**
     * @param peer_info The peer to send the data to.
     * @param buffer The data to send.
     * @return The status of send call and a span view of any data that wasn't sent.  This data if
     *         un-sent will correspond to bytes at the end of the given buffer.
     */
    template<concepts::const_buffer buffer_type>
    auto sendto(const net::socket_address& endpoint, const buffer_type& buffer) -> io_status
    {
        if (buffer.empty())
        {
            return io_status{io_status::kind::ok};
        }

        auto [sockaddr, socklen] = endpoint.data();

        auto bytes_sent = ::sendto(m_socket.native_handle(), buffer.data(), buffer.size(), 0, sockaddr, socklen);

        if (bytes_sent == std::ssize(buffer))
        {
            return io_status{io_status::kind::ok};
        }
        else
        {
            return make_io_status_from_native(errno);
        }
    }

    /**
     * @param buffer The buffer to receive data into.
     * @return The receive status, if ok then also the peer who sent the data and the data.
     *         The span view of the data will be set to the size of the received data, this will
     *         always start at the beggining of the buffer but depending on how large the data was
     *         it might not fill the entire buffer.
     */
    template<
        concepts::mutable_buffer buffer_type,
        typename element_type = typename concepts::mutable_buffer_traits<buffer_type>::element_type>
    auto recvfrom(buffer_type&& buffer) -> std::tuple<io_status, net::socket_address, std::span<element_type>>
    {
        // The user must bind locally to be able to receive packets.
        if (!m_bound)
        {
            return {
                io_status{io_status::kind::udp_not_bound},
                net::socket_address::make_uninitialised(),
                std::span<element_type>{}};
        }

        auto endpoint            = net::socket_address::make_uninitialised();
        auto [sockaddr, socklen] = endpoint.native_mutable_data();

        auto bytes_read = ::recvfrom(m_socket.native_handle(), buffer.data(), buffer.size(), 0, sockaddr, socklen);

        if (bytes_read < 0)
        {
            return {
                make_io_status_from_native(errno),
                net::socket_address::make_uninitialised(),
                std::span<element_type>{}};
        }

        return {
            io_status{io_status::kind::ok},
            endpoint,
            std::span<element_type>{buffer.data(), static_cast<size_t>(bytes_read)}};
    }

private:
    /// The scheduler that will drive this udp client.
    coro::scheduler* m_scheduler;
    /// The udp socket.
    net::socket m_socket{-1};
    /// Did the user request this udp socket is bound locally to receive packets?
    bool m_bound{false};
};

} // namespace coro::net::udp

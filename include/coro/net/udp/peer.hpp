#pragma once

#include "coro/concepts/buffer.hpp"
#include "coro/io_scheduler.hpp"
#include "coro/net/endpoint.hpp"
#include "coro/net/recv_status.hpp"
#include "coro/net/send_status.hpp"
#include "coro/net/socket.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <span>

namespace coro
{
class io_scheduler;
} // namespace coro

namespace coro::net::udp
{
class peer
{
public:
    /**
     * Creates a udp peer that can send packets but not receive them.  This udp peer will not explicitly
     * bind to a local ip+port.
     */
    explicit peer(std::unique_ptr<coro::io_scheduler>& scheduler, net::domain_t domain = net::domain_t::ipv4);

    /**
     * Creates a udp peer that can send and receive packets.  This peer will bind to the given ip_port.
     */
    explicit peer(std::unique_ptr<coro::io_scheduler>& scheduler, const net::endpoint& endpoint);

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

    /**
     * @param op The poll operation to perform on the udp socket. Note that if this is a send call only
     *           udp socket (did not bind) then polling for read will not work.
     * @param timeout The timeout for the poll operation to be ready.
     * @return The result status of the poll operation.
     */
    auto poll(poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<coro::poll_status>
    {
        co_return co_await m_io_scheduler->poll(m_socket, op, timeout);
    }

    /**
     * @param peer_info The peer to send the data to.
     * @param buffer The data to send.
     * @return The status of send call and a span view of any data that wasn't sent.  This data if
     *         un-sent will correspond to bytes at the end of the given buffer.
     */
    template<
        concepts::const_buffer buffer_type,
        typename element_type = typename concepts::const_buffer_traits<buffer_type>::element_type>
    auto sendto(const net::endpoint& endpoint, const buffer_type& buffer)
        -> std::pair<send_status, std::span<element_type>>
    {
        if (buffer.empty())
        {
            return {send_status::ok, std::span<element_type>{}};
        }

        auto [sockaddr, socklen] = endpoint.data();

        auto bytes_sent = ::sendto(m_socket.native_handle(), buffer.data(), buffer.size(), 0, sockaddr, socklen);

        if (bytes_sent >= 0)
        {
            return {send_status::ok, std::span<element_type>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
        }
        else
        {
            return {static_cast<send_status>(errno), std::span<element_type>{}};
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
    auto recvfrom(buffer_type&& buffer) -> std::tuple<recv_status, net::endpoint, std::span<element_type>>
    {
        // The user must bind locally to be able to receive packets.
        if (!m_bound)
        {
            return {recv_status::udp_not_bound, net::endpoint::make_uninitialised(), std::span<element_type>{}};
        }

        auto endpoint            = net::endpoint::make_uninitialised();
        auto [sockaddr, socklen] = endpoint.native_mutable_data();

        auto bytes_read = ::recvfrom(m_socket.native_handle(), buffer.data(), buffer.size(), 0, sockaddr, socklen);

        if (bytes_read < 0)
        {
            return {static_cast<recv_status>(errno), net::endpoint::make_uninitialised(), std::span<element_type>{}};
        }

        return {recv_status::ok, endpoint, std::span<element_type>{buffer.data(), static_cast<size_t>(bytes_read)}};
    }

private:
    /// The scheduler that will drive this udp client.
    coro::io_scheduler* m_io_scheduler;
    /// The udp socket.
    net::socket m_socket{-1};
    /// Did the user request this udp socket is bound locally to receive packets?
    bool m_bound{false};
};

} // namespace coro::net::udp

#pragma once

#include "coro/concepts/buffer.hpp"
#include "coro/io_scheduler.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/read_status.hpp"
#include "coro/net/recv_status.hpp"
#include "coro/net/send_status.hpp"
#include "coro/net/socket.hpp"
#include "coro/net/write_status.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <span>
#include <variant>

namespace coro
{
class io_scheduler;
} // namespace coro

namespace coro::net::udp
{
class peer
{
public:
    struct info
    {
        /// The ip address of the peer.
        net::ip_address address{net::ip_address::from_string("127.0.0.1")};
        /// The port of the peer.
        uint16_t port{8080};

        auto operator<=>(const info& other) const = default;
    };

    /**
     * Creates a udp peer that can send packets but not receive them.  This udp peer will not explicitly
     * bind to a local ip+port.
     */
    explicit peer(std::shared_ptr<io_scheduler> scheduler, net::domain_t domain = net::domain_t::ipv4);

    /**
     * Creates a udp peer that can send and receive packets.  This peer will bind to the given ip_port.
     */
    explicit peer(std::shared_ptr<io_scheduler> scheduler, const info& bind_info);

    peer(const peer&)                             = default;
    peer(peer&&)                                  = default;
    auto operator=(const peer&) noexcept -> peer& = default;
    auto operator=(peer&&) noexcept -> peer&      = default;
    ~peer()                                       = default;

#if defined(CORO_PLATFORM_UNIX)
    /**
     * @param op The poll operation to perform on the udp socket.  Note that if this is a send only
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
    template<concepts::const_buffer buffer_type>
    auto sendto(const info& peer_info, const buffer_type& buffer) -> std::pair<send_status, std::span<const char>>;

    /**
     * @param buffer The buffer to receive data into.
     * @return The receive status, if ok then also the peer who sent the data and the data.
     *         The span view of the data will be set to the size of the received data, this will
     *         always start at the beggining of the buffer but depending on how large the data was
     *         it might not fill the entire buffer.
     */
    template<concepts::mutable_buffer buffer_type>
    auto recvfrom(buffer_type&& buffer) -> std::tuple<recv_status, peer::info, std::span<char>>;

    auto write_to(
        const info&               peer_info,
        std::span<const char>     buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<write_status, std::span<const char>>>;

    auto read_from(std::span<char> buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::tuple<read_status, peer::info, std::span<char>>>;
#elif defined(CORO_PLATFORM_WINDOWS)

    auto write_to(
        const info&               peer_info,
        std::span<const char>     buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::pair<write_status, std::span<const char>>>;

    auto read_from(std::span<char> buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<std::tuple<read_status, peer::info, std::span<char>>>;

#endif

private:
    /// The scheduler that will drive this udp client.
    std::shared_ptr<io_scheduler> m_io_scheduler;
    /// The udp socket.
    net::socket m_socket{net::socket::invalid_handle};
    /// Did the user request this udp socket is bound locally to receive packets?
    bool m_bound{false};
};

#if defined(CORO_PLATFORM_UNIX)
template<concepts::const_buffer buffer_type>
auto peer::sendto(const info& peer_info, const buffer_type& buffer) -> std::pair<send_status, std::span<const char>>
{
    if (buffer.empty())
    {
        return {send_status::ok, std::span<const char>{}};
    }

    sockaddr_in peer{};
    peer.sin_family = static_cast<int>(peer_info.address.domain());
    peer.sin_port   = htons(peer_info.port);
    peer.sin_addr   = *reinterpret_cast<const in_addr*>(peer_info.address.data().data());

    socklen_t peer_len{sizeof(peer)};

    auto bytes_sent = ::sendto(
        m_socket.native_handle(), buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), peer_len);

    if (bytes_sent >= 0)
    {
        return {send_status::ok, std::span<const char>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
    }
    else
    {
        return {static_cast<send_status>(errno), std::span<const char>{}};
    }
}
template<concepts::mutable_buffer buffer_type>
auto peer::recvfrom(buffer_type&& buffer) -> std::tuple<recv_status, peer::info, std::span<char>>
{
    // The user must bind locally to be able to receive packets.
    if (!m_bound)
    {
        return {recv_status::udp_not_bound, peer::info{}, std::span<char>{}};
    }

    sockaddr_storage peer{};
    socklen_t        peer_len{sizeof(peer)};

    auto bytes_read = ::recvfrom(
        m_socket.native_handle(), buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), &peer_len);

    if (bytes_read < 0)
    {
        return {static_cast<recv_status>(errno), peer::info{}, std::span<char>{}};
    }

    auto&& [address, port] = ip_address::from_os(peer, peer_len);

    return {
        recv_status::ok,
        peer::info{
            .address = std::move(address),
            .port    = port
        },
        std::span<char>{buffer.data(), static_cast<size_t>(bytes_read)}};
}
inline auto peer::write_to(const info& peer_info, std::span<const char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<write_status, std::span<const char>>>
{
    if (auto status = co_await poll(poll_op::write, timeout); status != poll_status::event)
    {
        switch (status)
        {
            case poll_status::closed:
                co_return {write_status::closed, std::span<const char>{buffer.data(), buffer.size()}};
                ;
            case poll_status::error:
                co_return {write_status::error, std::span<const char>{buffer.data(), buffer.size()}};
            case poll_status::timeout:
                co_return {write_status::timeout, std::span<const char>{buffer.data(), buffer.size()}};
            default:
                throw std::runtime_error("Unknown poll_status value.");
        }
    }
    switch (auto&& [status, span] = sendto(peer_info, buffer); status)
    {
        case send_status::ok:
            co_return {write_status::ok, span};
        case send_status::closed:
            co_return {write_status::closed, span};
        default:
            co_return {write_status::error, span};
    }
}

inline auto peer::read_from(std::span<char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::tuple<read_status, peer::info, std::span<char>>>
{
    if (!m_bound)
    {
        co_return {read_status::udp_not_bound, peer::info{}, std::span<char>{}};
    }

    if (auto status = co_await poll(poll_op::read, timeout); status != poll_status::event)
    {
        switch (status)
        {
            case poll_status::closed:
                co_return {read_status::closed, peer::info{}, std::span<char>{}};
            case poll_status::error:
                co_return {read_status::error, peer::info{}, std::span<char>{}};
            case poll_status::timeout:
                co_return {read_status::timeout, peer::info{}, std::span<char>{}};
            default:
                throw std::runtime_error("Unknown poll_status value.");
        }
    }
    switch (auto&& [status, info, span] = recvfrom(buffer); status)
    {
        case recv_status::ok:
            co_return {read_status::ok, std::move(info), span};
        case recv_status::closed:
            co_return {read_status::closed, std::move(info), span};
        default:
            co_return {read_status::error, std::move(info), span};
    }
}
#endif

} // namespace coro::net::udp

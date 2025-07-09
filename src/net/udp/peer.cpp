#include "coro/net/udp/peer.hpp"

#if defined(CORO_PLATFORM_WINDOWS)
    // The order of includes matters
    // clang-format off
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include "coro/detail/iocp_overlapped.hpp"
// clang-format on
#endif

namespace coro::net::udp
{
peer::peer(std::shared_ptr<io_scheduler> scheduler, net::domain_t domain)
    : m_io_scheduler(std::move(scheduler)),
      m_socket(net::make_socket(net::socket::options{domain, net::socket::type_t::udp, net::socket::blocking_t::no}))
{
#if defined(CORO_PLATFORM_WINDOWS)
    // Bind socket to IOCP
    m_io_scheduler->bind_socket(m_socket);
#endif
}

peer::peer(std::shared_ptr<io_scheduler> scheduler, const info& bind_info)
    : m_io_scheduler(std::move(scheduler)),
      m_socket(
          net::make_accept_socket(
              net::socket::options{bind_info.address.domain(), net::socket::type_t::udp, net::socket::blocking_t::no},
              bind_info.address,
              bind_info.port)),
      m_bound(true)
{
#if defined(CORO_PLATFORM_WINDOWS)
    // Bind socket to IOCP
    m_io_scheduler->bind_socket(m_socket);
#endif
}

#if defined(CORO_PLATFORM_WINDOWS)
auto peer::write_to(const info& peer_info, std::span<const char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<write_status, std::span<const char>>>
{
    if (buffer.empty())
        co_return {write_status::ok, std::span<const char>{}};

    coro::detail::overlapped_io_operation ov{};
    WSABUF                                buf;
    buf.buf          = const_cast<char*>(buffer.data());
    buf.len          = buffer.size();
    DWORD bytes_sent = 0;

    sockaddr_storage server{};
    std::size_t      server_length{};
    peer_info.address.to_os(peer_info.port, server, server_length);

    int r = WSASendTo(
        reinterpret_cast<SOCKET>(m_socket.native_handle()),
        &buf,
        1,
        &bytes_sent,
        0,
        reinterpret_cast<sockaddr*>(&server),
        server_length,
        &ov.ov,
        nullptr);

    if (r == 0)
    {
        if (bytes_sent == 0)
            co_return {write_status::closed, buffer};
        co_return {write_status::ok, std::span<const char>{buffer.data() + bytes_sent, buffer.size() - bytes_sent}};
    }
    else if (WSAGetLastError() == WSA_IO_PENDING)
    {
        auto status = co_await m_io_scheduler->poll(ov.pi, timeout);
        if (status == poll_status::event)
        {
            co_return {
                write_status::ok,
                std::span<const char>{buffer.data() + ov.bytes_transferred, buffer.size() - ov.bytes_transferred}};
        }
        else if (status == poll_status::timeout)
        {
            BOOL success = CancelIoEx(static_cast<HANDLE>(m_socket.native_handle()), &ov.ov);
            if (!success)
            {
                int err = GetLastError();
                if (err == ERROR_NOT_FOUND)
                {
                    // Operation has been completed
                    co_return {
                        write_status::ok,
                        std::span<const char>{
                            buffer.data() + ov.bytes_transferred, buffer.size() - ov.bytes_transferred}};
                }
            }
            co_return {write_status::timeout, buffer};
        }
    }

    co_return {write_status::error, buffer};
}
auto peer::read_from(std::span<char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::tuple<read_status, peer::info, std::span<char>>>
{
    if (!m_bound)
    {
        co_return {read_status::udp_not_bound, peer::info{}, std::span<char>{}};
    }

    detail::overlapped_io_operation ov{};
    WSABUF                          buf;
    buf.buf     = buffer.data();
    buf.len     = buffer.size();
    DWORD flags = 0, bytes_recv = 0;

    sockaddr_storage remote{};
    socklen_t        remote_len = sizeof(remote);

    int r = WSARecvFrom(
        reinterpret_cast<SOCKET>(m_socket.native_handle()),
        &buf,
        1,
        &bytes_recv,
        &flags,
        reinterpret_cast<sockaddr*>(&remote),
        &remote_len,
        &ov.ov,
        nullptr);

    auto get_remote_info = [&remote]() -> peer::info
    {
        ip_address    remote_ip;
        std::uint16_t remote_port;
        if (remote.ss_family == AF_INET)
        {
            auto&     addr = reinterpret_cast<sockaddr_in&>(remote);
            std::span ip_addr_view{
                reinterpret_cast<std::uint8_t*>(&addr.sin_addr.s_addr), sizeof(addr.sin_addr.s_addr)};
            remote_ip   = ip_address{ip_addr_view, domain_t::ipv4};
            remote_port = ntohs(addr.sin_port);
        }
        else
        {
            auto&     addr = reinterpret_cast<sockaddr_in6&>(remote);
            std::span ip_addr_view{reinterpret_cast<std::uint8_t*>(&addr.sin6_addr), sizeof(addr.sin6_addr)};
            remote_ip   = ip_address{ip_addr_view, domain_t::ipv6};
            remote_port = ntohs(addr.sin6_port);
        }
        return peer::info{std::move(remote_ip), remote_port};
    };

    if (r == 0) // Data already read
    {
        if (bytes_recv == 0)
            co_return {read_status::closed, peer::info{}, buffer};
        co_return {read_status::ok, get_remote_info(), std::span<char>{buffer.data(), bytes_recv}};
    }
    else if (WSAGetLastError() == WSA_IO_PENDING)
    {
        auto status = co_await m_io_scheduler->poll(ov.pi, timeout);
        if (status == poll_status::event)
        {
            co_return {read_status::ok, get_remote_info(), std::span<char>{buffer.data(), ov.bytes_transferred}};
        }
        else if (status == poll_status::timeout)
        {
            BOOL success = CancelIoEx(reinterpret_cast<HANDLE>(m_socket.native_handle()), &ov.ov);
            if (!success)
            {
                int err = GetLastError();
                if (err == ERROR_NOT_FOUND)
                {
                    // Operation has been completed
                    co_return {
                        read_status::ok, get_remote_info(), std::span<char>{buffer.data(), ov.bytes_transferred}};
                }
            }
            co_return {read_status::timeout, peer::info{}, std::span<char>{}};
        }
    }

    co_return {read_status::error, peer::info{}, std::span<char>{}};
}
#endif

} // namespace coro::net::udp

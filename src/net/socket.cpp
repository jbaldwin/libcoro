#include "coro/net/socket.hpp"

#if defined(CORO_PLATFORM_WINDOWS)
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <Windows.h>
#elif defined(CORO_PLATFORM_UNIX)
    #include <sys/socket.h>
#endif

namespace coro::net
{

auto socket::type_to_os(type_t type) -> int
{
    switch (type)
    {
        case type_t::udp:
            return SOCK_DGRAM;
        case type_t::tcp:
            return SOCK_STREAM;
        default:
            throw std::runtime_error{"Unknown socket::type_t."};
    }
}

#if defined(CORO_PLATFORM_UNIX)
auto socket::operator=(const socket& other) noexcept -> socket&
{
    this->close();
    this->m_fd = dup(other.m_fd);
    return *this;
}
#endif

auto socket::operator=(socket&& other) noexcept -> socket&
{
    if (std::addressof(other) != this)
    {
        m_fd = std::exchange(other.m_fd, invalid_handle);
    }

    return *this;
}

auto socket::blocking(blocking_t block) -> bool
{
    if (!is_valid())
    {
        return false;
    }

#if defined(CORO_PLATFORM_UNIX)
    int flags = fcntl(m_fd, F_GETFL, 0);
    if (flags == -1)
    {
        return false;
    }

    // Add or subtract non-blocking flag.
    flags = (block == blocking_t::yes) ? flags & ~O_NONBLOCK : (flags | O_NONBLOCK);

    return (fcntl(m_fd, F_SETFL, flags) == 0);
#elif defined(CORO_PLATFORM_WINDOWS)
    u_long mode = (block == blocking_t::yes) ? 0 : 1;
    return ioctlsocket((SOCKET)m_fd, FIONBIO, &mode) == 0;
#endif
}

auto socket::shutdown(poll_op how) -> bool
{
    if (!is_valid())
    {
        return false;
    }

    int h = 0;
#if defined(CORO_PLATFORM_UNIX)
    // POSIX systems use SHUT_RD, SHUT_WR, SHUT_RDWR
    switch (how)
    {
        case poll_op::read:
            h = SHUT_RD;
            break;
        case poll_op::write:
            h = SHUT_WR;
            break;
        case poll_op::read_write:
            h = SHUT_RDWR;
            break;
    }
    return (::shutdown(m_fd, h) == 0);
#elif defined(_WIN32) || defined(_WIN64)
    // WinSock uses SD_RECEIVE, SD_SEND, SD_BOTH
    switch (how)
    {
        case poll_op::read:
            h = SD_RECEIVE;
            break;
        case poll_op::write:
            h = SD_SEND;
            break;
        case poll_op::read_write:
            h = SD_BOTH;
            break;
    }
    return (::shutdown((SOCKET)m_fd, h) == 0);
#endif
}

auto socket::close() -> void
{
    if (is_valid())
    {
#if defined(CORO_PLATFORM_UNIX)
        ::close(m_fd);
#elif defined(CORO_PLATFORM_WINDOWS)
        ::closesocket((SOCKET)m_fd);
#endif
        m_fd = socket::invalid_handle;
    }
}

auto make_socket(const socket::options& opts) -> socket
{
#if defined(CORO_PLATFORM_UNIX)
    socket s{::socket(domain_to_os(opts.domain), socket::type_to_os(opts.type), 0)};
    if (!s.is_valid())
    {
        throw std::runtime_error{"Failed to create socket."};
    }
#elif defined(CORO_PLATFORM_WINDOWS)
    auto   winsock = detail::initialise_winsock();
    socket s{reinterpret_cast<socket::native_handle_t>(
        ::WSASocketW(domain_to_os(opts.domain), socket::type_to_os(opts.type), 0, nullptr, 0, WSA_FLAG_OVERLAPPED))};
    if (!s.is_valid())
    {
        throw std::runtime_error{"Failed to create socket."};
    }

    // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS:
    //   Prevents completion packets from being queued to the IOCP if the operation completes synchronously,
    //   reducing unnecessary overhead for fast operations.
    // FILE_SKIP_SET_EVENT_ON_HANDLE:
    //   Prevents the system from setting the event in OVERLAPPED.hEvent upon operation completion,
    //   which is unnecessary when using IOCP and can improve performance by avoiding extra kernel event signals.
    const BOOL success = SetFileCompletionNotificationModes(
        reinterpret_cast<HANDLE>(s.native_handle()),
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE);
    if (!success)
    {
        throw std::runtime_error{"SetFileCompletionNotificationModes failed."};
    }
#endif

    if (opts.blocking == socket::blocking_t::no)
    {
        if (s.blocking(socket::blocking_t::no) == false)
        {
            throw std::runtime_error{"Failed to set socket to non-blocking mode."};
        }
    }

    return s;
}

auto make_accept_socket(const socket::options& opts, const net::ip_address& address, uint16_t port, int32_t backlog)
    -> socket
{
    socket s = make_socket(opts);

    int sock_opt{1};
    // BSD and macOS use a different SO_REUSEPORT implementation than Linux that enables both duplicate address and port
    // bindings with a single flag.
#if defined(CORO_PLATFORM_LINUX)
    using socket_t = decltype(s.native_handle());
    int  sock_opt_name = SO_REUSEADDR | SO_REUSEPORT;
    int* sock_opt_ptr  = &sock_opt;
#elif defined(CORO_PLATFORM_BSD)
    using socket_t = decltype(s.native_handle());
    int  sock_opt_name = SO_REUSEPORT;
    int* sock_opt_ptr  = &sock_opt;
#elif defined(CORO_PLATFORM_WINDOWS)
    using socket_t = SOCKET;
    int         sock_opt_name = SO_REUSEADDR;
    const char* sock_opt_ptr  = reinterpret_cast<const char*>(&sock_opt);
#endif

    if (setsockopt(reinterpret_cast<socket_t>(s.native_handle()), SOL_SOCKET, sock_opt_name, sock_opt_ptr, sizeof(sock_opt)) < 0)
    {
        throw std::runtime_error{"Failed to setsockopt."};
    }

    sockaddr_storage server{};
    std::size_t server_len{};
    address.to_os(port, server, server_len);

    if (bind(reinterpret_cast<socket_t>(s.native_handle()), reinterpret_cast<sockaddr*>(&server), server_len) < 0)
    {
        throw std::runtime_error{"Failed to bind."};
    }

    if (opts.type == socket::type_t::tcp)
    {
        if (listen(reinterpret_cast<socket_t>(s.native_handle()), backlog) < 0)
        {
            throw std::runtime_error{"Failed to listen."};
        }
    }

    return s;
}

} // namespace coro::net

#include "coro/net/socket.hpp"
#if defined(CORO_PLATFORM_WINDOWS)
    #include <Windows.h>
    #include <WinSock2.h>
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
        m_fd = std::exchange(other.m_fd, -1);
    }

    return *this;
}

auto socket::blocking(blocking_t block) -> bool
{
    if (m_fd < 0)
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
    return ioctlsocket(m_fd, FIONBIO, &mode) == 0;
#endif
}

auto socket::shutdown(poll_op how) -> bool
{
    if (m_fd == -1)
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
#endif
    return (::shutdown((SOCKET)m_fd, h) == 0);
}

auto socket::close() -> void
{
    if (m_fd != -1)
    {
        
#if defined(CORO_PLATFORM_UNIX)
        ::close(m_fd);
#elif defined(CORO_PLATFORM_WINDOWS)
        ::closesocket(m_fd);
#endif
        m_fd = socket::invalid_handle;
    }
}

auto make_socket(const socket::options& opts) -> socket
{
#if defined(CORO_PLATFORM_UNIX)
    socket s{::socket(static_cast<int>(opts.domain), socket::type_to_os(opts.type), 0)};
    if (s.native_handle() != socket::invalid_handle)
    {
        throw std::runtime_error{"Failed to create socket."};
    }
#elif defined(CORO_PLATFORM_WINDOWS)
    socket s{::WSASocketA(static_cast<int>(opts.domain), socket::type_to_os(opts.type), 0, NULL, 0, WSA_FLAG_OVERLAPPED)};
    if (s.native_handle() != INVALID_SOCKET)
    {
        throw std::runtime_error{"Failed to create socket."};
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
    int  sock_opt_name = SO_REUSEADDR | SO_REUSEPORT;
    int* sock_opt_ptr  = &sock_opt;
#elif defined(CORO_PLATFORM_BSD)
    int  sock_opt_name = SO_REUSEPORT;
    int* sock_opt_ptr  = &sock_opt;
#elif defined(CORO_PLATFORM_WINDOWS)
    int  sock_opt_name = SO_REUSEADDR;
    const char *sock_opt_ptr  = reinterpret_cast<const char*>(&sock_opt);
#endif

    if (setsockopt(s.native_handle(), SOL_SOCKET, sock_opt_name, sock_opt_ptr, sizeof(sock_opt)) < 0)
    {
        throw std::runtime_error{"Failed to setsockopt."};
    }

    sockaddr_in server{};
    server.sin_family = static_cast<int>(opts.domain);
    server.sin_port   = htons(port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(address.data().data());

    if (bind(s.native_handle(), reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0)
    {
        throw std::runtime_error{"Failed to bind."};
    }

    if (opts.type == socket::type_t::tcp)
    {
        if (listen(s.native_handle(), backlog) < 0)
        {
            throw std::runtime_error{"Failed to listen."};
        }
    }

    return s;
}

} // namespace coro::net

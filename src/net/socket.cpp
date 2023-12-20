#include "coro/net/socket.hpp"

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

auto socket::operator=(const socket& other) noexcept -> socket&
{
    this->close();
    this->m_fd = dup(other.m_fd);
    return *this;
}

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

    int flags = fcntl(m_fd, F_GETFL, 0);
    if (flags == -1)
    {
        return false;
    }

    // Add or subtract non-blocking flag.
    flags = (block == blocking_t::yes) ? flags & ~O_NONBLOCK : (flags | O_NONBLOCK);

    return (fcntl(m_fd, F_SETFL, flags) == 0);
}

auto socket::shutdown(poll_op how) -> bool
{
    if (m_fd != -1)
    {
        int h{0};
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
    }
    return false;
}

auto socket::close() -> void
{
    if (m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

auto make_socket(const socket::options& opts) -> socket
{
    socket s{::socket(static_cast<int>(opts.domain), socket::type_to_os(opts.type), 0)};
    if (s.native_handle() < 0)
    {
        throw std::runtime_error{"Failed to create socket."};
    }

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
    if (setsockopt(s.native_handle(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &sock_opt, sizeof(sock_opt)) < 0)
    {
        throw std::runtime_error{"Failed to setsockopt(SO_REUSEADDR | SO_REUSEPORT)"};
    }

    sockaddr_in server{};
    server.sin_family = static_cast<int>(opts.domain);
    server.sin_port   = htons(port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(address.data().data());

    if (bind(s.native_handle(), (struct sockaddr*)&server, sizeof(server)) < 0)
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

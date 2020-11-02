#pragma once

#include "coro/poll.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <utility>

#include <iostream>

namespace coro
{
class socket
{
public:
    enum class domain_t
    {
        ipv4,
        ipv6
    };

    enum class type_t
    {
        udp,
        tcp
    };

    enum class blocking_t
    {
        yes,
        no
    };

    struct options
    {
        domain_t   domain;
        type_t     type;
        blocking_t blocking;
    };

    static auto domain_to_os(const domain_t& domain) -> int
    {
        switch (domain)
        {
            case domain_t::ipv4:
                return AF_INET;
            case domain_t::ipv6:
                return AF_INET6;
            default:
                throw std::runtime_error{"Unknown socket::domain_t."};
        }
    }

    static auto type_to_os(const type_t& type) -> int
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

    static auto make_socket(const options& opts) -> socket
    {
        socket s{::socket(domain_to_os(opts.domain), type_to_os(opts.type), 0)};
        if (s.native_handle() < 0)
        {
            throw std::runtime_error{"Failed to create socket."};
        }

        if (opts.blocking == blocking_t::no)
        {
            if (!s.blocking(blocking_t::no))
            {
                throw std::runtime_error{"Failed to set socket to non-blocking mode."};
            }
        }

        return s;
    }

    static auto make_accept_socket(
        const options&     opts,
        const std::string& address, // force string to guarantee null terminated.
        uint16_t           port,
        int32_t            backlog = 128) -> socket
    {
        socket s = make_socket(opts);

        int sock_opt{1};
        if (setsockopt(s.native_handle(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &sock_opt, sizeof(sock_opt)) < 0)
        {
            throw std::runtime_error{"Failed to setsockopt(SO_REUSEADDR | SO_REUSEPORT)"};
        }

        sockaddr_in server{};
        server.sin_family      = domain_to_os(opts.domain);
        server.sin_addr.s_addr = htonl(inet_addr(address.data()));
        server.sin_port        = htons(port);

        if (bind(s.native_handle(), (struct sockaddr*)&server, sizeof(server)) < 0)
        {
            throw std::runtime_error{"Failed to bind."};
        }

        if (listen(s.native_handle(), backlog) < 0)
        {
            throw std::runtime_error{"Failed to listen."};
        }

        return s;
    }

    socket() = default;

    explicit socket(int fd) : m_fd(fd) {}

    socket(const socket&) = delete;
    socket(socket&& other) : m_fd(std::exchange(other.m_fd, -1)) {}

    auto operator=(const socket&) -> socket& = delete;
    auto operator                            =(socket&& other) noexcept -> socket&
    {
        if (std::addressof(other) != this)
        {
            m_fd = std::exchange(other.m_fd, -1);
        }

        return *this;
    }

    ~socket() { close(); }

    auto blocking(blocking_t block) -> bool
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

    auto recv(std::span<char> buffer) -> ssize_t { return ::read(m_fd, buffer.data(), buffer.size()); }

    auto send(const std::span<const char> buffer) -> ssize_t { return ::write(m_fd, buffer.data(), buffer.size()); }

    auto shutdown(poll_op how = poll_op::read_write) -> bool
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

    auto close() -> void
    {
        if (m_fd != -1)
        {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    auto native_handle() const -> int { return m_fd; }

private:
    int m_fd{-1};
};

} // namespace coro

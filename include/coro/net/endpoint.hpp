#pragma once

#include "ip_address.hpp"
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
namespace coro::net
{
class endpoint
{
public:
    endpoint(std::string_view ip, std::uint16_t port, domain_t domain = domain_t::ipv4) : endpoint(ip_address::from_string(ip, domain), port) {}

    endpoint(const ip_address& ip, std::uint16_t port)
    {
        std::memset(&m_storage, 0, sizeof(m_storage));

        if (ip.domain() == domain_t::ipv4)
        {
            auto* sin       = reinterpret_cast<sockaddr_in*>(&m_storage);
            sin->sin_family = AF_INET;
            sin->sin_port   = htons(port);
            std::memcpy(&sin->sin_addr, ip.data().data(), sizeof(in_addr));
            m_len = sizeof(sockaddr_in);
        }
        else if (ip.domain() == domain_t::ipv6)
        {
            auto* sin6        = reinterpret_cast<sockaddr_in6*>(&m_storage);
            sin6->sin6_family = AF_INET6;
            sin6->sin6_port   = htons(port);
            std::memcpy(&sin6->sin6_addr, ip.data().data(), sizeof(in6_addr));
            m_len = sizeof(sockaddr_in6);

            // TODO: link-local addresses
            // sin6->sin6_scope_id = ip.scope_id();
        }
    }

    [[nodiscard]] auto data() const& -> std::pair<const sockaddr*, socklen_t>
    {
        return {reinterpret_cast<const sockaddr*>(&m_storage), m_len};
    }
    auto data() const&& -> std::pair<const sockaddr*, socklen_t> = delete;

    [[nodiscard]] auto native_mutable_data() & -> std::pair<sockaddr*, socklen_t*>
    {
        return {reinterpret_cast<sockaddr*>(&m_storage), &m_len};
    }

    [[nodiscard]] auto domain() const -> domain_t
    {
        if (m_storage.ss_family == AF_INET)
            return domain_t::ipv4;
        if (m_storage.ss_family == AF_INET6)
            return domain_t::ipv6;
        throw std::runtime_error{"coro::net::endpoint::domain() Invalid domain"};
    }

    [[nodiscard]] auto port() const -> std::uint16_t
    {
        if (m_storage.ss_family == AF_INET)
            return ntohs(reinterpret_cast<const sockaddr_in*>(&m_storage)->sin_port);
        if (m_storage.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&m_storage)->sin6_port);
        throw std::runtime_error{"coro::net::endpoint::port() Invalid domain"};
    }

    auto operator ==(const endpoint &other) const -> bool {
        return m_len == other.m_len && std::memcmp(&m_storage, &other.m_storage, m_len) == 0;
    }

    static auto from_accept(const sockaddr_storage& storage, socklen_t len) -> endpoint
    {
        endpoint ep;
        ep.m_storage = storage;
        ep.m_len     = len;
        return ep;
    }

    static auto make_uninitialised() -> endpoint { return endpoint{}; }

private:
    endpoint() { std::memset(&m_storage, 0, sizeof(m_storage)); }

    sockaddr_storage m_storage{};
    socklen_t        m_len{};
};
} // namespace coro::net
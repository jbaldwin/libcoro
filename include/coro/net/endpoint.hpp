#pragma once

#include "ip_address.hpp"
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
namespace coro::net
{
/**
 * Represents IP address and port.
 */
class endpoint
{
public:
    endpoint(std::string_view ip, std::uint16_t port, domain_t domain = domain_t::ipv4)
        : endpoint(ip_address::from_string(ip, domain), port)
    {
    }

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

    /**
     * @brief Gets a pointer to underlying sockaddr structure.
     * Suitable for systemcalls like connect(), bind() or sendto().
     * @return A pair containing the const sockaddr pointer and its length.
     */
    [[nodiscard]] auto data() const& -> std::pair<const sockaddr*, socklen_t>
    {
        return {reinterpret_cast<const sockaddr*>(&m_storage), m_len};
    }

    /// Prevent usage on temporary objects to avoid dangling pointers.
    auto data() const&& -> std::pair<const sockaddr*, socklen_t> = delete;

    /**
     * @brief Provides access to the storage for modification.
     * Suitable for system calls like accept() or recvfrom().
     * @return A pair containing the sockaddr pointer and a pointer to its length.
     * @see make_unitialised()
     */
    [[nodiscard]] auto native_mutable_data() & -> std::pair<sockaddr*, socklen_t*>
    {
        return {reinterpret_cast<sockaddr*>(&m_storage), &m_len};
    }

    /**
     * @brief Extracts the ip_address from the endpoint.
     * @return An ip_address object
     * @throws std::runtime_error If the address family is not supported
     */
    [[nodiscard]] auto ip() const -> ip_address
    {
        if (domain() == domain_t::ipv4)
        {
            auto* sin = reinterpret_cast<const sockaddr_in*>(&m_storage);
            return ip_address{
                {reinterpret_cast<const uint8_t*>(&sin->sin_addr), sizeof(sin->sin_addr)}, domain_t::ipv4};
        }
        if (domain() == domain_t::ipv6)
        {
            auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&m_storage);
            return ip_address{
                {reinterpret_cast<const uint8_t*>(&sin6->sin6_addr), sizeof(sin6->sin6_addr)}, domain_t::ipv6};
        }
        throw std::runtime_error{"coro::net::endpoint::ip() Invalid domain"};
    }

    /**
     * @brief Extracts the address family from the endpoint.
     * @return An domain_t object
     * @throws std::runtime_error If the address family is not supported
     */
    [[nodiscard]] auto domain() const -> domain_t
    {
        if (m_storage.ss_family == AF_INET)
            return domain_t::ipv4;
        if (m_storage.ss_family == AF_INET6)
            return domain_t::ipv6;
        throw std::runtime_error{"coro::net::endpoint::domain() Invalid domain"};
    }

    /**
     * @brief Extracts the the port from the endpoint.
     * @return The port number in host byte order.
     * @throws std::runtime_error If the address family is not supported
     */
    [[nodiscard]] auto port() const -> std::uint16_t
    {
        if (m_storage.ss_family == AF_INET)
            return ntohs(reinterpret_cast<const sockaddr_in*>(&m_storage)->sin_port);
        if (m_storage.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&m_storage)->sin6_port);
        throw std::runtime_error{"coro::net::endpoint::port() Invalid domain"};
    }

    auto operator==(const endpoint& other) const -> bool
    {
        return m_len == other.m_len && std::memcmp(&m_storage, &other.m_storage, m_len) == 0;
    }

    /**
     * @brief Creates an empty endpoint for late initialisation.
     */
    static auto make_uninitialised() -> endpoint { return endpoint{}; }

private:
    // It's private to avoid default empty initialisation and to make use more explicit make_uninitialised
    endpoint() { std::memset(&m_storage, 0, sizeof(m_storage)); }

    sockaddr_storage m_storage{};
    socklen_t        m_len{};
};
} // namespace coro::net
#include "coro/net/ip_address.hpp"
#include <coro/platform.hpp>
#include <cstring>

#if defined(CORO_PLATFORM_UNIX)
    #include <arpa/inet.h>
#elif defined(CORO_PLATFORM_WINDOWS)
    #include <WS2tcpip.h>
    #include <WinSock2.h>
    #include <Windows.h>
#endif

namespace coro::net
{
static std::string domain_ipv4{"ipv4"};
static std::string domain_ipv6{"ipv6"};

auto domain_to_os(domain_t domain) -> int
{
    switch (domain)
    {
        case domain_t::ipv4:
            return AF_INET;
        case domain_t::ipv6:
            return AF_INET6;
    }
    throw std::runtime_error{"coro::net::to_string(domain_t) unknown domain"};
}

auto to_string(domain_t domain) -> const std::string&
{
    switch (domain)
    {
        case domain_t::ipv4:
            return domain_ipv4;
        case domain_t::ipv6:
            return domain_ipv6;
    }
    throw std::runtime_error{"coro::net::to_string(domain_t) unknown domain"};
}

auto ip_address::from_string(const std::string& address, domain_t domain) -> ip_address
{
    ip_address addr{};
    addr.m_domain = domain;

    auto success = inet_pton(domain_to_os(addr.m_domain), address.data(), addr.m_data.data());
    if (success != 1)
    {
        throw std::runtime_error{"coro::net::ip_address faild to convert from string"};
    }

    return addr;
}

auto ip_address::to_string() const -> std::string
{
    std::string output;
    if (m_domain == domain_t::ipv4)
    {
        output.resize(INET_ADDRSTRLEN, '\0');
    }
    else
    {
        output.resize(INET6_ADDRSTRLEN, '\0');
    }

    auto success = inet_ntop(domain_to_os(m_domain), m_data.data(), output.data(), output.length());
    if (success != nullptr)
    {
        auto len = strnlen(success, output.length());
        output.resize(len);
    }
    else
    {
        throw std::runtime_error{"coro::net::ip_address failed to convert to string representation"};
    }

    return output;
}
auto ip_address::to_os(const std::uint16_t port, sockaddr_storage& storage, std::size_t& len) const -> void
{
    switch (domain())
    {
        case domain_t::ipv4:
        {
            auto& addr      = reinterpret_cast<sockaddr_in&>(storage);
            addr.sin_family = domain_to_os(domain());
            addr.sin_addr   = *reinterpret_cast<const in_addr*>(data().data());
            addr.sin_port   = htons(port);
            len             = sizeof(sockaddr_in);
            return;
        }
        case domain_t::ipv6:
        {
            auto& addr         = reinterpret_cast<sockaddr_in6&>(storage);
            addr.sin6_family   = domain_to_os(domain());
            addr.sin6_addr     = *reinterpret_cast<const in6_addr*>(data().data());
            addr.sin6_port     = htons(port);
            addr.sin6_flowinfo = 0;
            addr.sin6_scope_id = 0;
            len                = sizeof(sockaddr_in6);
            return;
        }
        default:
            throw std::runtime_error{"coro::net::ip_address unknown domain"};
    }
}
auto ip_address::from_os(const sockaddr_storage& storage, std::size_t len) -> std::pair<ip_address, std::uint16_t>
{
    if (storage.ss_family == AF_INET)
    {
        auto&           addr = reinterpret_cast<const sockaddr_in&>(storage);
        const std::span ip_addr_view{
            reinterpret_cast<const std::uint8_t*>(&addr.sin_addr.s_addr), sizeof(addr.sin_addr.s_addr)};

        return {ip_address{ip_addr_view, domain_t::ipv4}, ntohs(addr.sin_port)};
    }
    else
    {
        auto&           addr = reinterpret_cast<const sockaddr_in6&>(storage);
        const std::span ip_addr_view{reinterpret_cast<const std::uint8_t*>(&addr.sin6_addr), sizeof(addr.sin6_addr)};

        return {ip_address{ip_addr_view, domain_t::ipv6}, ntohs(addr.sin6_port)};
    }
}
auto ip_address::get_any_address(domain_t domain) -> ip_address
{
    switch (domain)
    {
        case domain_t::ipv4:
            return from_string("0.0.0.0", domain);
        case domain_t::ipv6:
            return from_string("::", domain);
        default:
            throw std::runtime_error{"coro::net::ip_address unknown domain"};
    }
}

} // namespace coro::net

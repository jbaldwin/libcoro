#pragma once

#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace coro::net
{
enum class domain_t : int
{
    ipv4 = AF_INET,
    ipv6 = AF_INET6
};

auto to_string(domain_t domain) -> const std::string&;

class ip_address
{
public:
    static const constexpr size_t ipv4_len{4};
    static const constexpr size_t ipv6_len{16};

    ip_address() = default;
    ip_address(std::span<const uint8_t> binary_address, domain_t domain = domain_t::ipv4) : m_domain(domain)
    {
        if (m_domain == domain_t::ipv4 && binary_address.size() > ipv4_len)
        {
            throw std::runtime_error{"coro::net::ip_address provided binary ip address is too long"};
        }
        else if (binary_address.size() > ipv6_len)
        {
            throw std::runtime_error{"coro::net::ip_address provided binary ip address is too long"};
        }

        std::copy(binary_address.begin(), binary_address.end(), m_data.begin());
    }
    ip_address(const ip_address&) = default;
    ip_address(ip_address&&)      = default;
    auto operator=(const ip_address&) noexcept -> ip_address& = default;
    auto operator=(ip_address&&) noexcept -> ip_address& = default;
    ~ip_address()                                        = default;

    auto domain() const -> domain_t { return m_domain; }
    auto data() const -> std::span<const uint8_t>
    {
        if (m_domain == domain_t::ipv4)
        {
            return std::span<const uint8_t>{m_data.data(), ipv4_len};
        }
        else
        {
            return std::span<const uint8_t>{m_data.data(), ipv6_len};
        }
    }

    static auto from_string(const std::string& address, domain_t domain = domain_t::ipv4) -> ip_address
    {
        ip_address addr{};
        addr.m_domain = domain;

        auto success = inet_pton(static_cast<int>(addr.m_domain), address.data(), addr.m_data.data());
        if (success != 1)
        {
            throw std::runtime_error{"coro::net::ip_address faild to convert from string"};
        }

        return addr;
    }

    auto to_string() const -> std::string
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

        auto success = inet_ntop(static_cast<int>(m_domain), m_data.data(), output.data(), output.length());
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

    auto operator<=>(const ip_address& other) const = default;

private:
    domain_t                      m_domain{domain_t::ipv4};
    std::array<uint8_t, ipv6_len> m_data{};
};

} // namespace coro::net

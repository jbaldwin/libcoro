#include "coro/net/ip_address.hpp"

namespace coro::net
{
static std::string domain_ipv4{"ipv4"};
static std::string domain_ipv6{"ipv6"};

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

} // namespace coro::net

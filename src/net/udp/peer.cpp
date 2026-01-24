#include "coro/net/udp/peer.hpp"
#include <memory>
#include <utility>

namespace coro::net::udp
{
peer::peer(std::unique_ptr<coro::scheduler>& scheduler, net::domain_t domain)
    : m_scheduler(scheduler.get()),
      m_socket(net::make_socket(net::socket::options{domain, net::socket::type_t::udp, net::socket::blocking_t::no}))
{
    if (m_scheduler == nullptr)
    {
        throw std::runtime_error("udp::peer cannot have nullptr scheduler");
    }
}

peer::peer(std::unique_ptr<coro::scheduler>& scheduler, const info& bind_info)
    : m_scheduler(scheduler.get()),
      m_socket(
          net::make_accept_socket(
              net::socket::options{bind_info.address.domain(), net::socket::type_t::udp, net::socket::blocking_t::no},
              bind_info.address,
              bind_info.port)),
      m_bound(true)
{
    if (m_scheduler == nullptr)
    {
        throw std::runtime_error("udp::peer cannot have nullptr scheduler");
    }
}

peer::peer(peer&& other) noexcept
    : m_scheduler(std::exchange(other.m_scheduler, nullptr)),
      m_socket(std::move(other.m_socket)),
      m_bound(other.m_bound)
{
}

auto peer::operator=(peer&& other) noexcept -> peer&
{
    if (std::addressof(other) != this)
    {
        m_scheduler = std::exchange(other.m_scheduler, nullptr);
        m_socket       = std::move(other.m_socket);
        m_bound        = other.m_bound;
    }
    return *this;
}

auto peer::operator=(const peer& other) noexcept -> peer&
{
    if (std::addressof(other) != this)
    {
        m_scheduler = other.m_scheduler;
        m_socket       = other.m_socket;
        m_bound        = other.m_bound;
    }
    return *this;
}
} // namespace coro::net::udp

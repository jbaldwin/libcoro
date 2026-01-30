#include "coro/net/udp/peer.hpp"
#include <memory>
#include <utility>

namespace coro::net::udp
{
peer::peer(std::unique_ptr<coro::io_scheduler>& scheduler, net::domain_t domain)
    : m_io_scheduler(scheduler.get()),
      m_socket(net::make_socket(net::socket::options{net::socket::type_t::udp, net::socket::blocking_t::no}, domain))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error("udp::peer cannot have nullptr scheduler");
    }
}

peer::peer(std::unique_ptr<coro::io_scheduler>& scheduler, const net::endpoint& endpoint)
    : m_io_scheduler(scheduler.get()),
      m_socket(
          net::make_accept_socket(
              net::socket::options{net::socket::type_t::udp, net::socket::blocking_t::no}, endpoint, 32)),
      m_bound(true)
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error("udp::peer cannot have nullptr scheduler");
    }
}

peer::peer(peer&& other) noexcept
    : m_io_scheduler(std::exchange(other.m_io_scheduler, nullptr)),
      m_socket(std::move(other.m_socket)),
      m_bound(other.m_bound)
{
}

auto peer::operator=(peer&& other) noexcept -> peer&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler = std::exchange(other.m_io_scheduler, nullptr);
        m_socket       = std::move(other.m_socket);
        m_bound        = other.m_bound;
    }
    return *this;
}

auto peer::operator=(const peer& other) noexcept -> peer&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler = other.m_io_scheduler;
        m_socket       = other.m_socket;
        m_bound        = other.m_bound;
    }
    return *this;
}
} // namespace coro::net::udp

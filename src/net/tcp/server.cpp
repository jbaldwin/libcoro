#include "coro/net/tcp/server.hpp"

#include "coro/scheduler.hpp"

namespace coro::net::tcp
{
server::server(std::unique_ptr<coro::scheduler>& scheduler, const net::socket_address& endpoint, options opts)
    : m_scheduler(scheduler.get()),
      m_options(std::move(opts)),
      m_accept_socket(
          net::make_accept_socket(
              net::socket::options{socket::type_t::tcp, net::socket::blocking_t::no}, endpoint, m_options.backlog))
{
    if (m_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::server cannot have a nullptr scheduler"};
    }
}

server::server(server&& other)
    : m_scheduler(std::exchange(other.m_scheduler, nullptr)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_scheduler     = std::exchange(other.m_scheduler, nullptr);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

} // namespace coro::net::tcp

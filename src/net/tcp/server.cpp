#include "coro/net/tcp/server.hpp"

#include "coro/io_scheduler.hpp"

namespace coro::net::tcp
{
server::server(std::unique_ptr<coro::io_scheduler>& scheduler, const net::endpoint &endpoint, options opts)
    : m_io_scheduler(scheduler.get()),
      m_options(std::move(opts)),
      m_accept_socket(
          net::make_accept_socket(
              net::socket::options{socket::type_t::tcp, net::socket::blocking_t::no},
              endpoint,
              m_options.backlog))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::server cannot have a nullptr io_scheduler"};
    }
}

server::server(server&& other)
    : m_io_scheduler(std::exchange(other.m_io_scheduler, nullptr)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::exchange(other.m_io_scheduler, nullptr);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

auto server::accept() -> coro::net::tcp::client
{
    auto client_endpoint = endpoint::make_uninitialised();
    auto [sockaddr, socklen] = client_endpoint.native_mutable_data();

    net::socket s{::accept(m_accept_socket.native_handle(), sockaddr, socklen)};

    return tcp::client{m_io_scheduler, std::move(s), client_endpoint};
};

} // namespace coro::net::tcp

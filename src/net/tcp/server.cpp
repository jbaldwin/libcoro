#include "coro/net/tcp/server.hpp"

#include "coro/io_scheduler.hpp"

namespace coro::net::tcp
{
server::server(std::shared_ptr<io_scheduler> scheduler, options opts)
    : m_io_scheduler(std::move(scheduler)),
      m_options(std::move(opts)),
      m_accept_socket(net::make_accept_socket(
          net::socket::options{net::domain_t::ipv4, net::socket::type_t::tcp, net::socket::blocking_t::no},
          m_options.address,
          m_options.port,
          m_options.backlog))
{
    if (m_io_scheduler == nullptr)
    {
        throw std::runtime_error{"tcp::server cannot have a nullptr io_scheduler"};
    }
}

server::server(server&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto server::operator=(server&& other) -> server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::move(other.m_io_scheduler);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

auto server::accept() -> coro::net::tcp::client
{
    sockaddr_in         client{};
    constexpr const int len = sizeof(struct sockaddr_in);
    net::socket         s{::accept(
        m_accept_socket.native_handle(),
        reinterpret_cast<struct sockaddr*>(&client),
        const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&len)))};

    std::span<const uint8_t> ip_addr_view{
        reinterpret_cast<uint8_t*>(&client.sin_addr.s_addr),
        sizeof(client.sin_addr.s_addr),
    };

    return tcp::client{
        m_io_scheduler,
        std::move(s),
        client::options{
            .address = net::ip_address{ip_addr_view, static_cast<net::domain_t>(client.sin_family)},
            .port    = ntohs(client.sin_port),
        }};
};

} // namespace coro::net::tcp

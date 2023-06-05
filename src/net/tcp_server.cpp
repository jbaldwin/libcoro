#include "coro/net/tcp_server.hpp"

#include "coro/io_scheduler.hpp"

namespace coro::net
{
tcp_server::tcp_server(std::shared_ptr<io_scheduler> scheduler, options opts)
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
        throw std::runtime_error{"tcp_server cannot have a nullptr io_scheduler"};
    }
}

tcp_server::tcp_server(tcp_server&& other)
    : m_io_scheduler(std::move(other.m_io_scheduler)),
      m_options(std::move(other.m_options)),
      m_accept_socket(std::move(other.m_accept_socket))
{
}

auto tcp_server::operator=(tcp_server&& other) -> tcp_server&
{
    if (std::addressof(other) != this)
    {
        m_io_scheduler  = std::move(other.m_io_scheduler);
        m_options       = std::move(other.m_options);
        m_accept_socket = std::move(other.m_accept_socket);
    }
    return *this;
}

auto tcp_server::accept() -> coro::net::tcp_client
{
    sockaddr_in         client{};
    constexpr const int len = sizeof(struct sockaddr_in);
    net::socket         s{::accept(m_accept_socket.native_handle(), (struct sockaddr*)&client, (socklen_t*)&len)};

    std::span<const uint8_t> ip_addr_view{
        reinterpret_cast<uint8_t*>(&client.sin_addr.s_addr),
        sizeof(client.sin_addr.s_addr),
    };

    return tcp_client{
        m_io_scheduler,
        std::move(s),
        tcp_client::options{
            .address = net::ip_address{ip_addr_view, static_cast<net::domain_t>(client.sin_family)},
            .port    = ntohs(client.sin_port),
#ifdef LIBCORO_FEATURE_SSL
            .ssl_ctx = m_options.ssl_ctx
#endif
        }};
};

} // namespace coro::net

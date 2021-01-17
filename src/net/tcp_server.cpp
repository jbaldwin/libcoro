#include "coro/net/tcp_server.hpp"

namespace coro::net
{
tcp_server::tcp_server(io_scheduler& scheduler, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_accept_socket(net::make_accept_socket(
          net::socket::options{net::domain_t::ipv4, net::socket::type_t::tcp, net::socket::blocking_t::no},
          m_options.address,
          m_options.port,
          m_options.backlog))
{
}

auto tcp_server::poll(std::chrono::milliseconds timeout) -> coro::task<coro::poll_status>
{
    co_return co_await m_io_scheduler.poll(m_accept_socket, coro::poll_op::read, timeout);
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
            .port    = ntohs(client.sin_port)}};
};

} // namespace coro::net

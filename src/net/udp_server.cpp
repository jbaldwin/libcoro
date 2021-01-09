#include "coro/net/udp_server.hpp"
#include "coro/io_scheduler.hpp"

namespace coro::net
{

udp_server::udp_server(io_scheduler& io_scheduler, options opts)
    : m_io_scheduler(io_scheduler),
      m_options(std::move(opts)),
      m_accept_socket(net::make_accept_socket(
          net::socket::options{m_options.address.domain(), net::socket::type_t::udp, net::socket::blocking_t::no},
          m_options.address,
          m_options.port
      ))
{

}

auto udp_server::recvfrom(std::span<char>& buffer, std::chrono::milliseconds timeout) -> coro::task<std::optional<udp_client::options>>
{
    auto pstatus = co_await m_io_scheduler.poll(m_accept_socket, poll_op::read, timeout);
    if(pstatus != poll_status::event)
    {
        co_return std::nullopt;
    }

    sockaddr_in client{};

    socklen_t client_len{sizeof(client)};

    auto bytes_read = ::recvfrom(
        m_accept_socket.native_handle(),
        buffer.data(),
        buffer.size(),
        0,
        reinterpret_cast<sockaddr*>(&client),
        &client_len);

    if(bytes_read == -1)
    {
        co_return std::nullopt;
    }

    buffer = buffer.subspan(0, bytes_read);

    std::span<const uint8_t> ip_addr_view{
        reinterpret_cast<uint8_t*>(&client.sin_addr.s_addr),
        sizeof(client.sin_addr.s_addr),
    };

    co_return udp_client::options{
                .address = net::ip_address{ip_addr_view, static_cast<net::domain_t>(client.sin_family)},
                .port = ntohs(client.sin_port)
            };
}

} // namespace coro::net

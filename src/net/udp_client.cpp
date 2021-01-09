#include "coro/net/udp_client.hpp"
#include "coro/io_scheduler.hpp"

namespace coro::net
{

udp_client::udp_client(io_scheduler& scheduler, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_socket({net::make_socket(net::socket::options{m_options.address.domain(), net::socket::type_t::udp, net::socket::blocking_t::no})})
{

}

auto udp_client::sendto(const std::span<const char> buffer, std::chrono::milliseconds timeout) -> coro::task<ssize_t>
{
    auto pstatus = co_await m_io_scheduler.poll(m_socket, poll_op::write, timeout);
    if(pstatus != poll_status::event)
    {
        co_return 0;
    }

    sockaddr_in server{};
    server.sin_family = static_cast<int>(m_options.address.domain());
    server.sin_port   = htons(m_options.port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(m_options.address.data().data());

    socklen_t server_len{sizeof(server)};

    co_return ::sendto(
        m_socket.native_handle(),
        buffer.data(),
        buffer.size(),
        0,
        reinterpret_cast<sockaddr*>(&server),
        server_len);
}

} // namespace coro::net

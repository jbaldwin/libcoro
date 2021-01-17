#include "coro/net/tcp_client.hpp"
#include "coro/io_scheduler.hpp"

#include <ares.h>

namespace coro::net
{
using namespace std::chrono_literals;

tcp_client::tcp_client(io_scheduler& scheduler, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_socket(net::make_socket(
          net::socket::options{m_options.address.domain(), net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
}

tcp_client::tcp_client(io_scheduler& scheduler, net::socket socket, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_socket(std::move(socket)),
      m_connect_status(connect_status::connected)
{
}

auto tcp_client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    if (m_connect_status.has_value() && m_connect_status.value() == connect_status::connected)
    {
        co_return m_connect_status.value();
    }

    sockaddr_in server{};
    server.sin_family = static_cast<int>(m_options.address.domain());
    server.sin_port   = htons(m_options.port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(m_options.address.data().data());

    auto cret = ::connect(m_socket.native_handle(), (struct sockaddr*)&server, sizeof(server));
    if (cret == 0)
    {
        // Immediate connect.
        m_connect_status = connect_status::connected;
        co_return connect_status::connected;
    }
    else if (cret == -1)
    {
        // If the connect is happening in the background poll for write on the socket to trigger
        // when the connection is established.
        if (errno == EAGAIN || errno == EINPROGRESS)
        {
            auto pstatus = co_await m_io_scheduler.poll(m_socket.native_handle(), poll_op::write, timeout);
            if (pstatus == poll_status::event)
            {
                int       result{0};
                socklen_t result_length{sizeof(result)};
                if (getsockopt(m_socket.native_handle(), SOL_SOCKET, SO_ERROR, &result, &result_length) < 0)
                {
                    std::cerr << "connect failed to getsockopt after write poll event\n";
                }

                if (result == 0)
                {
                    // success, connected
                    m_connect_status = connect_status::connected;
                    co_return connect_status::connected;
                }
            }
            else if (pstatus == poll_status::timeout)
            {
                m_connect_status = connect_status::timeout;
                co_return connect_status::timeout;
            }
        }
    }

    m_connect_status = connect_status::error;
    co_return connect_status::error;
}

} // namespace coro::net

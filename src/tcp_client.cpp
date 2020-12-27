#include "coro/tcp_client.hpp"
#include "coro/io_scheduler.hpp"

namespace coro
{
tcp_client::tcp_client(io_scheduler& scheduler, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_socket(socket::make_socket(socket::options{m_options.domain, socket::type_t::tcp, socket::blocking_t::yes}))
{
}

auto tcp_client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    sockaddr_in server{};
    server.sin_family = socket::domain_to_os(m_options.domain);
    server.sin_port   = htons(m_options.port);

    if (inet_pton(server.sin_family, m_options.address.data(), &server.sin_addr) <= 0)
    {
        co_return connect_status::invalid_ip_address;
    }

    auto cret = ::connect(m_socket.native_handle(), (struct sockaddr*)&server, sizeof(server));
    if (cret == 0)
    {
        // Immediate connect.
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
                    co_return connect_status::connected;
                }
            }
            else if (pstatus == poll_status::timeout)
            {
                co_return connect_status::timeout;
            }
        }
    }

    co_return connect_status::error;
}

} // namespace coro

#include "coro/net/tcp_client.hpp"
#include "coro/io_scheduler.hpp"

#include <ares.h>

namespace coro::net
{
tcp_client::tcp_client(io_scheduler& scheduler, options opts)
    : m_io_scheduler(scheduler),
      m_options(std::move(opts)),
      m_socket(net::socket::make_socket(net::socket::options{m_options.domain, net::socket::type_t::tcp, net::socket::blocking_t::no}))
{
}

auto tcp_client::connect(std::chrono::milliseconds timeout) -> coro::task<connect_status>
{
    if(m_connect_status.has_value() && m_connect_status.value() == connect_status::connected)
    {
        co_return m_connect_status.value();
    }

    const net::ip_address* ip_addr{nullptr};
    std::unique_ptr<net::dns_result> result_ptr{nullptr};

    // If the user provided a hostname then perform the dns lookup.
    if(std::holds_alternative<net::hostname>(m_options.address))
    {
        if(m_options.dns == nullptr)
        {
            m_connect_status = connect_status::dns_client_required;
            co_return connect_status::dns_client_required;
        }
        const auto& hn = std::get<net::hostname>(m_options.address);
        result_ptr = co_await m_options.dns->host_by_name(hn);
        if(result_ptr->status() != net::dns_status::complete)
        {
            m_connect_status = connect_status::dns_lookup_failure;
            co_return connect_status::dns_lookup_failure;
        }

        if(result_ptr->ip_addresses().empty())
        {
            m_connect_status = connect_status::dns_lookup_failure;
            co_return connect_status::dns_lookup_failure;
        }

        // TODO: for now we'll just take the first ip address given, but should probably allow the
        // user to take preference on ipv4/ipv6 addresses.
        ip_addr = &result_ptr->ip_addresses().front();
    }
    else
    {
        ip_addr = &std::get<net::ip_address>(m_options.address);
    }

    sockaddr_in server{};
    server.sin_family = static_cast<int>(m_options.domain);
    server.sin_port   = htons(m_options.port);
    server.sin_addr   = *reinterpret_cast<const in_addr*>(ip_addr->data().data());

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

#include "coro/net/dns_resolver.hpp"

#include <iostream>
#include <netdb.h>
#include <arpa/inet.h>

namespace coro::net
{

uint64_t dns_resolver::m_ares_count{0};
std::mutex dns_resolver::m_ares_mutex{};

auto ares_dns_callback(
    void* arg,
    int status,
    int /*timeouts*/,
    struct hostent* host
) -> void
{
    auto& result = *static_cast<dns_result*>(arg);
    --result.m_pending_dns_requests;

    if(host == nullptr || status != ARES_SUCCESS)
    {
        result.m_status = dns_status::error;
    }
    else
    {
        result.m_status = dns_status::complete;

        for(size_t i = 0; host->h_addr_list[i] != nullptr; ++i)
        {
            size_t len = (host->h_addrtype == AF_INET) ? net::ip_address::ipv4_len : net::ip_address::ipv6_len;
            net::ip_address ip_addr{
                std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(host->h_addr_list[i]), len},
                static_cast<net::domain_t>(host->h_addrtype)
            };

            result.m_ip_addresses.emplace_back(std::move(ip_addr));
        }
    }

    if(result.m_pending_dns_requests == 0)
    {
        result.m_token.resume();
    }
}

dns_result::dns_result(coro::resume_token<void>& token, uint64_t pending_dns_requests)
    : m_token(token),
      m_pending_dns_requests(pending_dns_requests)
{

}

dns_resolver::dns_resolver(io_scheduler& scheduler, std::chrono::milliseconds timeout)
    : m_scheduler(scheduler),
      m_timeout(timeout)
{
    {
        std::lock_guard<std::mutex> g{m_ares_mutex};
        if(m_ares_count == 0)
        {
            auto ares_status = ares_library_init(ARES_LIB_INIT_ALL);
            if(ares_status != ARES_SUCCESS)
            {
                throw std::runtime_error{ares_strerror(ares_status)};
            }
        }
        ++m_ares_count;
    }

    auto channel_init_status = ares_init(&m_ares_channel);
    if(channel_init_status != ARES_SUCCESS)
    {
        throw std::runtime_error{ares_strerror(channel_init_status)};
    }
}

dns_resolver::~dns_resolver()
{
    if(m_ares_channel != nullptr)
    {
        ares_destroy(m_ares_channel);
        m_ares_channel = nullptr;
    }

    {
        std::lock_guard<std::mutex> g{m_ares_mutex};
        --m_ares_count;
        if(m_ares_count == 0)
        {
            ares_library_cleanup();
        }
    }
}

auto dns_resolver::host_by_name(const net::hostname& hn) -> coro::task<std::unique_ptr<dns_result>>
{
    auto token = m_scheduler.make_resume_token<void>();
    auto result_ptr = std::make_unique<dns_result>(token, 2);

    ares_gethostbyname(m_ares_channel, hn.data().data(), AF_INET, ares_dns_callback, result_ptr.get());
    ares_gethostbyname(m_ares_channel, hn.data().data(), AF_INET6, ares_dns_callback, result_ptr.get());

    // Add all required poll calls for ares to kick off the dns requests.
    ares_poll();

    // Suspend until this specific result is completed by ares.
    co_await m_scheduler.yield(token);
    co_return result_ptr;
}

auto dns_resolver::ares_poll() -> void
{
    std::array<ares_socket_t, ARES_GETSOCK_MAXNUM> ares_sockets{};
    std::array<poll_op, ARES_GETSOCK_MAXNUM> poll_ops{};

    int bitmask = ares_getsock(m_ares_channel, ares_sockets.data(), ARES_GETSOCK_MAXNUM);

    size_t new_sockets{0};

    for(size_t i = 0; i < ARES_GETSOCK_MAXNUM; ++i)
    {
        uint64_t ops{0};

        if(ARES_GETSOCK_READABLE(bitmask, i))
        {
            ops |= static_cast<uint64_t>(poll_op::read);
        }
        if(ARES_GETSOCK_WRITABLE(bitmask, i))
        {
            ops |= static_cast<uint64_t>(poll_op::write);
        }

        if(ops != 0)
        {
            poll_ops[i] = static_cast<poll_op>(ops);
            ++new_sockets;
        }
        else
        {
            // According to ares usage within curl once a bitmask for a socket is zero the rest of
            // the bitmask will also be zero.
            break;
        }
    }

    for(size_t i = 0; i < new_sockets; ++i)
    {
        io_scheduler::fd_t fd = static_cast<io_scheduler::fd_t>(ares_sockets[i]);

        // If this socket is not currently actively polling, start polling!
        if(m_active_sockets.emplace(fd).second)
        {
            m_scheduler.schedule(make_poll_task(fd, poll_ops[i]));
        }
    }
}

auto dns_resolver::make_poll_task(io_scheduler::fd_t fd, poll_op ops) -> coro::task<void>
{
    auto result = co_await m_scheduler.poll(fd, ops, m_timeout);
    switch(result)
    {
        case poll_status::event:
        {
            auto read_sock = poll_op_readable(ops) ? fd : ARES_SOCKET_BAD;
            auto write_sock = poll_op_writeable(ops) ? fd : ARES_SOCKET_BAD;
            ares_process_fd(m_ares_channel, read_sock, write_sock);
        }
            break;
        case poll_status::timeout:
            ares_process_fd(m_ares_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
            break;
        case poll_status::closed:
            // might need to do something like call with two ARES_SOCKET_BAD?
            break;
        case poll_status::error:
            // might need to do something like call with two ARES_SOCKET_BAD?
            break;
    }

    // Remove from the list of actively polling sockets.
    m_active_sockets.erase(fd);

    // Re-initialize sockets/polls for ares since this one has now triggered.
    ares_poll();

    co_return;
};

} // namespace coro::net

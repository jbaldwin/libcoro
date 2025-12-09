#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/event.hpp"
#include "coro/fd.hpp"
#include "coro/net/hostname.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/poll.hpp"
#include "coro/task.hpp"

#include <ares.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace coro::net::dns
{
namespace detail
{
/// Global count to track if c-ares has been initialized or cleaned up.
static uint64_t m_ares_count;
/// Critical section around the c-ares global init/cleanup to prevent heap corruption.
static std::mutex m_ares_mutex;
} // namespace detail

template<concepts::io_executor executor_type>
class resolver;

enum class status
{
    complete,
    error
};

template<concepts::io_executor executor_type>
class result
{
    friend resolver<executor_type>;

public:
    result(std::unique_ptr<executor_type>& executor, coro::event& resume, uint64_t pending_dns_requests)
        : m_executor(executor),
          m_resume(resume),
          m_pending_dns_requests(pending_dns_requests)
    {
    }
    ~result() = default;

    /**
     * @return The status of the dns lookup.
     */
    auto status() const -> dns::status { return m_status; }

    /**
     * @return If the result of the dns looked was successful then the list of ip addresses that
     *         were resolved from the hostname.
     */
    auto ip_addresses() const -> const std::vector<coro::net::ip_address>& { return m_ip_addresses; }

private:
    std::unique_ptr<executor_type>&    m_executor;
    coro::event&                       m_resume;
    uint64_t                           m_pending_dns_requests{0};
    dns::status                        m_status{dns::status::complete};
    std::vector<coro::net::ip_address> m_ip_addresses{};

    friend auto ares_dns_callback(void* arg, int status, int timeouts, ares_addrinfo* addr_info) -> void;
};

template<concepts::io_executor executor_type>
class resolver
{
public:
    explicit resolver(std::unique_ptr<executor_type>& executor, std::chrono::milliseconds timeout)
        : m_executor(executor),
          m_timeout(timeout)
    {
        if (m_executor == nullptr)
        {
            throw std::runtime_error{"dns resolver cannot have nullptr executor"};
        }

        {
            std::scoped_lock g{detail::m_ares_mutex};
            if (detail::m_ares_count == 0)
            {
                auto ares_status = ares_library_init(ARES_LIB_INIT_ALL);
                if (ares_status != ARES_SUCCESS)
                {
                    throw std::runtime_error{ares_strerror(ares_status)};
                }
            }
            ++detail::m_ares_count;
        }

        ares_options options{};
        options.sock_state_cb      = resolver::ares_socket_state_callback;
        options.sock_state_cb_data = this;

        auto channel_init_status = ares_init_options(&m_ares_channel, &options, ARES_OPT_SOCK_STATE_CB);
        if (channel_init_status != ARES_SUCCESS)
        {
            throw std::runtime_error{ares_strerror(channel_init_status)};
        }
    }

    resolver(const resolver&)                             = delete;
    resolver(resolver&&)                                  = delete;
    auto operator=(const resolver&) noexcept -> resolver& = delete;
    auto operator=(resolver&&) noexcept -> resolver&      = delete;

    ~resolver()
    {
        if (m_ares_channel != nullptr)
        {
            ares_destroy(m_ares_channel);
            m_ares_channel = nullptr;
        }

        {
            std::scoped_lock g{detail::m_ares_mutex};
            --detail::m_ares_count;
            if (detail::m_ares_count == 0)
            {
                ares_library_cleanup();
            }
        }
    }

    /**
     * @param hn The hostname to resolve its ip addresses.
     */
    auto host_by_name(const net::hostname& hn) -> coro::task<std::unique_ptr<result<executor_type>>>
    {
        coro::event resume_event{};
        auto        result_ptr = std::make_unique<result<executor_type>>(m_executor, resume_event, 1);

        ares_addrinfo_hints hints{};
        hints.ai_family = AF_UNSPEC; // Request both IPv4 and IPv6

        ares_getaddrinfo(
            m_ares_channel,
            hn.data().data(),
            nullptr, // service name (port number or NULL)
            &hints,
            ares_dns_callback,
            result_ptr.get());

        // Suspend until this specific result is completed by ares.
        co_await resume_event;
        co_return result_ptr;
    }

private:
    /// The executor to drive the events for dns lookups.
    std::unique_ptr<executor_type>& m_executor;

    /// The global timeout per dns lookup request.
    std::chrono::milliseconds m_timeout{0};

    /// The libc-ares channel for looking up dns entries.
    ares_channel m_ares_channel{nullptr};

    /// This is the set of sockets that are currently being actively polled so multiple poll tasks
    /// are not setup when ares_poll() is called.
    std::unordered_set<fd_t> m_active_sockets{};

    auto make_poll_task(fd_t fd, poll_op ops) -> coro::task<void>
    {
        // The loop ensures non-blocking polling until the socket is closed by c-ares.
        while (m_active_sockets.contains(fd))
        {
            auto result = co_await m_executor->poll(fd, ops, m_timeout);
            switch (result)
            {
                case poll_status::event:
                {
                    auto read_sock  = poll_op_readable(ops) ? fd : ARES_SOCKET_BAD;
                    auto write_sock = poll_op_writeable(ops) ? fd : ARES_SOCKET_BAD;
                    ares_process_fd(m_ares_channel, read_sock, write_sock);
                }
                break;
                case poll_status::timeout:
                    ares_process_fd(m_ares_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                    break;
                case poll_status::closed:
                    // might need to do something like call with two ARES_SOCKET_BAD?
                    m_active_sockets.erase(fd);
                    break;
                case poll_status::error:
                    // might need to do something like call with two ARES_SOCKET_BAD?
                    m_active_sockets.erase(fd);
                    break;
            }
        }

        co_return;
    }

    static auto ares_socket_state_callback(void* data, ares_socket_t socket_fd, int readable, int writable) -> void
    {
        resolver* self = static_cast<resolver*>(data);
        uint64_t  ops{0};

        if (readable)
        {
            ops |= static_cast<uint64_t>(poll_op::read);
        }
        if (writable)
        {
            ops |= static_cast<uint64_t>(poll_op::write);
        }

        auto fd       = static_cast<fd_t>(socket_fd);
        auto poll_ops = static_cast<poll_op>(ops);
        if (ops != 0)
        {
            if (self->m_active_sockets.emplace(fd).second)
            {
                self->m_executor->spawn_detached(self->make_poll_task(fd, poll_ops));
            }
        }
        else
        {
            self->m_active_sockets.erase(fd);
        }
    }

    static auto ares_dns_callback(void* arg, int status, int /*timeouts*/, ares_addrinfo* addr_info) -> void
    {
        auto& result = *static_cast<coro::net::dns::result<executor_type>*>(arg);
        --result.m_pending_dns_requests;

        if (addr_info == nullptr || status != ARES_SUCCESS)
        {
            result.m_status = status::error;
        }
        else
        {
            result.m_status = status::complete;

            for (ares_addrinfo_node* node = addr_info->nodes; node != nullptr; node = node->ai_next)
            {
                if (node->ai_family == AF_INET)
                {
                    sockaddr_in*    sin = reinterpret_cast<sockaddr_in*>(node->ai_addr);
                    net::ip_address ip_addr{
                        std::span<const uint8_t>{
                            reinterpret_cast<const uint8_t*>(&sin->sin_addr), net::ip_address::ipv4_len},
                        static_cast<net::domain_t>(AF_INET)};

                    result.m_ip_addresses.emplace_back(std::move(ip_addr));
                }
                else if (node->ai_family == AF_INET6)
                {
                    sockaddr_in6*   sin6 = reinterpret_cast<sockaddr_in6*>(node->ai_addr);
                    net::ip_address ip_addr{
                        std::span<const uint8_t>{
                            reinterpret_cast<const uint8_t*>(&sin6->sin6_addr), net::ip_address::ipv6_len},
                        static_cast<net::domain_t>(AF_INET6)};

                    result.m_ip_addresses.emplace_back(std::move(ip_addr));
                }
            }

            ares_freeaddrinfo(addr_info);
        }

        if (result.m_pending_dns_requests == 0)
        {
            result.m_resume.set(result.m_executor, resume_order_policy::lifo);
        }
    }
};

} // namespace coro::net::dns

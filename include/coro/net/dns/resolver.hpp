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
#include <sys/epoll.h>
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

template<concepts::io_exceutor executor_type>
class resolver;

enum class status
{
    complete,
    error
};

template<concepts::io_exceutor executor_type>
class result
{
    friend resolver<executor_type>;

public:
    result(executor_type& executor, coro::event& resume, uint64_t pending_dns_requests)
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
    executor_type&                     m_executor;
    coro::event&                       m_resume;
    uint64_t                           m_pending_dns_requests{0};
    dns::status                        m_status{dns::status::complete};
    std::vector<coro::net::ip_address> m_ip_addresses{};

    friend auto ares_dns_callback(void* arg, int status, int timeouts, struct hostent* host) -> void;
};

template<concepts::io_exceutor executor_type>
class resolver
{
public:
    explicit resolver(std::shared_ptr<executor_type> executor, std::chrono::milliseconds timeout)
        : m_executor(std::move(executor)),
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

        auto channel_init_status = ares_init(&m_ares_channel);
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
        auto        result_ptr = std::make_unique<result<executor_type>>(*m_executor.get(), resume_event, 2);

        ares_gethostbyname(m_ares_channel, hn.data().data(), AF_INET, ares_dns_callback, result_ptr.get());
        ares_gethostbyname(m_ares_channel, hn.data().data(), AF_INET6, ares_dns_callback, result_ptr.get());

        // Add all required poll calls for ares to kick off the dns requests.
        ares_poll();

        // Suspend until this specific result is completed by ares.
        co_await resume_event;
        co_return result_ptr;
    }

private:
    /// The executor to drive the events for dns lookups.
    std::shared_ptr<executor_type> m_executor;

    /// The global timeout per dns lookup request.
    std::chrono::milliseconds m_timeout{0};

    /// The libc-ares channel for looking up dns entries.
    ares_channel m_ares_channel{nullptr};

    /// This is the set of sockets that are currently being actively polled so multiple poll tasks
    /// are not setup when ares_poll() is called.
    std::unordered_set<fd_t> m_active_sockets{};

    auto ares_poll() -> void
    {
        std::array<ares_socket_t, ARES_GETSOCK_MAXNUM> ares_sockets{};
        std::array<poll_op, ARES_GETSOCK_MAXNUM>       poll_ops{};

        int bitmask = ares_getsock(m_ares_channel, ares_sockets.data(), ARES_GETSOCK_MAXNUM);

        size_t new_sockets{0};

        for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; ++i)
        {
            uint64_t ops{0};

            if (ARES_GETSOCK_READABLE(bitmask, i))
            {
                ops |= static_cast<uint64_t>(poll_op::read);
            }
            if (ARES_GETSOCK_WRITABLE(bitmask, i))
            {
                ops |= static_cast<uint64_t>(poll_op::write);
            }

            if (ops != 0)
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

        std::vector<coro::task<void>> poll_tasks{};
        for (size_t i = 0; i < new_sockets; ++i)
        {
            auto fd = static_cast<fd_t>(ares_sockets[i]);

            // If this socket is not currently actively polling, start polling!
            if (m_active_sockets.emplace(fd).second)
            {
                m_executor->spawn(make_poll_task(fd, poll_ops[i]));
            }
        }
    }

    auto make_poll_task(fd_t fd, poll_op ops) -> coro::task<void>
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
    }

    static auto ares_dns_callback(void* arg, int status, int /*timeouts*/, struct hostent* host) -> void
    {
        auto& result = *static_cast<coro::net::dns::result<executor_type>*>(arg);
        --result.m_pending_dns_requests;

        if (host == nullptr || status != ARES_SUCCESS)
        {
            result.m_status = status::error;
        }
        else
        {
            result.m_status = status::complete;

            for (size_t i = 0; host->h_addr_list[i] != nullptr; ++i)
            {
                size_t len = (host->h_addrtype == AF_INET) ? net::ip_address::ipv4_len : net::ip_address::ipv6_len;
                net::ip_address ip_addr{
                    std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(host->h_addr_list[i]), len},
                    static_cast<net::domain_t>(host->h_addrtype)};

                result.m_ip_addresses.emplace_back(std::move(ip_addr));
            }
        }

        if (result.m_pending_dns_requests == 0)
        {
            result.m_resume.set(result.m_executor);
        }
    }
};

} // namespace coro::net::dns

#pragma once

#include "coro/fd.hpp"
#include "coro/io_scheduler.hpp"
#include "coro/net/hostname.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/task.hpp"
#include "coro/task_container.hpp"

#include <ares.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
#include <unordered_set>
#include <vector>

namespace coro::net
{
class dns_resolver;

enum class dns_status
{
    complete,
    error
};

class dns_result
{
    friend dns_resolver;

public:
    dns_result(coro::io_scheduler& scheduler, coro::event& resume, uint64_t pending_dns_requests);
    ~dns_result() = default;

    /**
     * @return The status of the dns lookup.
     */
    auto status() const -> dns_status { return m_status; }

    /**
     * @return If the result of the dns looked was successful then the list of ip addresses that
     *         were resolved from the hostname.
     */
    auto ip_addresses() const -> const std::vector<coro::net::ip_address>& { return m_ip_addresses; }

private:
    coro::io_scheduler&                m_io_scheduler;
    coro::event&                       m_resume;
    uint64_t                           m_pending_dns_requests{0};
    dns_status                         m_status{dns_status::complete};
    std::vector<coro::net::ip_address> m_ip_addresses{};

    friend auto ares_dns_callback(void* arg, int status, int timeouts, struct hostent* host) -> void;
};

class dns_resolver
{
public:
    explicit dns_resolver(std::shared_ptr<io_scheduler> scheduler, std::chrono::milliseconds timeout);
    dns_resolver(const dns_resolver&) = delete;
    dns_resolver(dns_resolver&&)      = delete;
    auto operator=(const dns_resolver&) noexcept -> dns_resolver& = delete;
    auto operator=(dns_resolver&&) noexcept -> dns_resolver& = delete;
    ~dns_resolver();

    /**
     * @param hn The hostname to resolve its ip addresses.
     */
    auto host_by_name(const net::hostname& hn) -> coro::task<std::unique_ptr<dns_result>>;

private:
    /// The io scheduler to drive the events for dns lookups.
    std::shared_ptr<io_scheduler> m_io_scheduler;

    /// The global timeout per dns lookup request.
    std::chrono::milliseconds m_timeout{0};

    /// The libc-ares channel for looking up dns entries.
    ares_channel m_ares_channel{nullptr};

    /// This is the set of sockets that are currently being actively polled so multiple poll tasks
    /// are not setup when ares_poll() is called.
    std::unordered_set<fd_t> m_active_sockets{};

    task_container<io_scheduler> m_task_container;

    /// Global count to track if c-ares has been initialized or cleaned up.
    static uint64_t m_ares_count;
    /// Critical section around the c-ares global init/cleanup to prevent heap corruption.
    static std::mutex m_ares_mutex;

    auto ares_poll() -> void;
    auto make_poll_task(fd_t fd, poll_op ops) -> coro::task<void>;
};

} // namespace coro::net

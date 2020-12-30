#pragma once

#include "coro/io_scheduler.hpp"
#include "coro/net/ip_address.hpp"
#include "coro/net/hostname.hpp"
#include "coro/task.hpp"

#include <ares.h>

#include <mutex>
#include <functional>
#include <vector>
#include <array>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <sys/epoll.h>

namespace coro
{

class dns_client;

enum class dns_status
{
    complete,
    error
};

class dns_result
{
    friend dns_client;
public:
    explicit dns_result(coro::resume_token<void>& token, uint64_t pending_dns_requests);
    ~dns_result() = default;

    auto status() const -> dns_status { return m_status; }
    auto ip_addresses() const -> const std::vector<coro::net::ip_address>& { return m_ip_addresses; }
private:
    coro::resume_token<void>& m_token;
    uint64_t m_pending_dns_requests{0};
    dns_status m_status{dns_status::complete};
    std::vector<coro::net::ip_address> m_ip_addresses{};

    friend auto ares_dns_callback(
        void* arg,
        int status,
        int timeouts,
        struct hostent* host
    ) -> void;
};

class dns_client
{
public:
    explicit dns_client(io_scheduler& scheduler, std::chrono::milliseconds timeout);
    dns_client(const dns_client&) = delete;
    dns_client(dns_client&&) = delete;
    auto operator=(const dns_client&) noexcept -> dns_client& = delete;
    auto operator=(dns_client&&) noexcept -> dns_client& = delete;
    ~dns_client();

    auto host_by_name(const net::hostname& hn) -> coro::task<std::unique_ptr<dns_result>>;
private:
    /// The io scheduler to drive the events for dns lookups.
    io_scheduler& m_scheduler;

    /// The global timeout per dns lookup request.
    std::chrono::milliseconds m_timeout{0};

    /// The libc-ares channel for looking up dns entries.
    ares_channel m_ares_channel{nullptr};

    /// This is the set of sockets that are currently being actively polled so multiple poll tasks
    /// are not setup when ares_poll() is called.
    std::unordered_set<io_scheduler::fd_t> m_active_sockets{};

    /// Global count to track if c-ares has been initialized or cleaned up.
    static uint64_t m_ares_count;
    /// Critical section around the c-ares global init/cleanup to prevent heap corruption.
    static std::mutex m_ares_mutex;

    auto ares_poll() -> void;
    auto make_poll_task(io_scheduler::fd_t fd, poll_op ops) -> coro::task<void>;
};

} // namespace coro

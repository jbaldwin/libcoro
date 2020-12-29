#pragma once

#include "coro/net/ip_address.hpp"
#include "coro/io_scheduler.hpp"
#include "coro/net/socket.hpp"
#include "coro/task.hpp"

#include <fcntl.h>
#include <functional>
#include <sys/socket.h>

namespace coro
{
class tcp_scheduler : public io_scheduler
{
public:
    using on_connection_t = std::function<task<void>(tcp_scheduler&, net::socket)>;

    struct options
    {
        net::ip_address       address       = net::ip_address::from_string("0.0.0.0");
        uint16_t              port          = 8080;
        int32_t               backlog       = 128;
        on_connection_t       on_connection = nullptr;
        io_scheduler::options io_options{};
    };

    explicit tcp_scheduler(
        options opts =
            options{
                net::ip_address::from_string("0.0.0.0"),
                8080,
                128,
                [](tcp_scheduler&, net::socket) -> task<void> { co_return; },
                io_scheduler::options{9, 2, io_scheduler::thread_strategy_t::spawn}});

    tcp_scheduler(const tcp_scheduler&) = delete;
    tcp_scheduler(tcp_scheduler&&)      = delete;
    auto operator=(const tcp_scheduler&) -> tcp_scheduler& = delete;
    auto operator=(tcp_scheduler&&) -> tcp_scheduler& = delete;

    ~tcp_scheduler() override;

    auto empty() const -> bool { return size() == 0; }

    auto size() const -> size_t
    {
        // Take one off for the accept task so the user doesn't have to account for the hidden task.
        auto size = io_scheduler::size();
        return (size > 0) ? size - 1 : 0;
    }

    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) -> void override;

private:
    options m_opts;

    /// Should the accept task continue accepting new connections?
    std::atomic<bool> m_accept_new_connections{true};
    std::atomic<bool> m_accept_task_exited{false};
    net::socket       m_accept_socket{-1};

    auto make_accept_task() -> coro::task<void>;
};

} // namespace coro

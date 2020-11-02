#pragma once

#include "coro/io_scheduler.hpp"
#include "coro/socket.hpp"
#include "coro/task.hpp"

#include <fcntl.h>
#include <functional>
#include <sys/socket.h>

#include <iostream>

namespace coro
{
class tcp_scheduler : public io_scheduler
{
public:
    using on_connection_t = std::function<task<void>(tcp_scheduler&, socket)>;

    struct options
    {
        std::string           address       = "0.0.0.0";
        uint16_t              port          = 8080;
        int32_t               backlog       = 128;
        on_connection_t       on_connection = nullptr;
        io_scheduler::options io_options{};
    };

    tcp_scheduler(
        options opts =
            options{
                "0.0.0.0",
                8080,
                128,
                [](tcp_scheduler&, socket) -> task<void> { co_return; },
                io_scheduler::options{9, 2, io_scheduler::thread_strategy_t::spawn}})
        : io_scheduler(std::move(opts.io_options)),
          m_opts(std::move(opts)),
          m_accept_socket(socket::make_accept_socket(
              socket::options{socket::domain_t::ipv4, socket::type_t::tcp, socket::blocking_t::no},
              m_opts.address,
              m_opts.port,
              m_opts.backlog))
    {
        if (m_opts.on_connection == nullptr)
        {
            throw std::runtime_error{"options::on_connection cannot be nullptr."};
        }

        schedule(make_accept_task());
    }

    tcp_scheduler(const tcp_scheduler&) = delete;
    tcp_scheduler(tcp_scheduler&&)      = delete;
    auto operator=(const tcp_scheduler&) -> tcp_scheduler& = delete;
    auto operator=(tcp_scheduler &&) -> tcp_scheduler& = delete;

    ~tcp_scheduler() override = default;

    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) -> void override
    {
        if (m_accept_new_connections.exchange(false, std::memory_order_release))
        {
            m_accept_socket.shutdown(); // wake it up by shutting down read/write operations.

            while (m_accept_task_exited.load(std::memory_order_acquire) == false)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }

            io_scheduler::shutdown(wait_for_tasks);
        }
    }

private:
    options m_opts;

    std::atomic<bool> m_accept_new_connections{true};
    std::atomic<bool> m_accept_task_exited{false};
    socket            m_accept_socket{-1};

    auto make_accept_task() -> coro::task<void>
    {
        sockaddr_in         client{};
        constexpr const int len = sizeof(struct sockaddr_in);

        std::vector<task<void>> tasks{};
        tasks.reserve(16);

        while (m_accept_new_connections.load(std::memory_order::acquire))
        {
            co_await poll(m_accept_socket.native_handle(), coro::poll_op::read);
            // auto status = co_await poll(m_accept_socket.native_handle(), coro::poll_op::read);
            // (void)status; // TODO: introduce timeouts on io_scheduer.poll();

            // On accept socket read drain the listen accept queue.
            while (true)
            {
                socket s{::accept(m_accept_socket.native_handle(), (struct sockaddr*)&client, (socklen_t*)&len)};
                if (s.native_handle() < 0)
                {
                    break;
                }

                tasks.emplace_back(m_opts.on_connection(std::ref(*this), std::move(s)));
            }

            if (!tasks.empty())
            {
                schedule(tasks);
                tasks.clear();
            }
        }

        m_accept_task_exited.exchange(true, std::memory_order::release);

        co_return;
    };
};

} // namespace coro

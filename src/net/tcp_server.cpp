#include "coro/net/tcp_server.hpp"

namespace coro::net
{

tcp_server::tcp_server(options opts)
    : io_scheduler(std::move(opts.io_options)),
        m_opts(std::move(opts)),
        m_accept_socket(net::socket::make_accept_socket(
            net::socket::options{net::domain_t::ipv4, net::socket::type_t::tcp, net::socket::blocking_t::no},
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

tcp_server::~tcp_server()
{
    shutdown();
}

auto tcp_server::shutdown(shutdown_t wait_for_tasks) -> void
{
    if (m_accept_new_connections.exchange(false, std::memory_order::release))
    {
        m_accept_socket.shutdown(); // wake it up by shutting down read/write operations.

        while (m_accept_task_exited.load(std::memory_order::acquire) == false)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        io_scheduler::shutdown(wait_for_tasks);
    }
}

auto tcp_server::make_accept_task() -> coro::task<void>
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
            net::socket s{::accept(m_accept_socket.native_handle(), (struct sockaddr*)&client, (socklen_t*)&len)};
            if (s.native_handle() < 0)
            {
                break;
            }

            tasks.emplace_back(m_opts.on_connection(std::ref(*this), std::move(s)));
        }

        if (!tasks.empty())
        {
            schedule(std::move(tasks));
        }
    }

    m_accept_task_exited.exchange(true, std::memory_order::release);

    co_return;
};

} // namespace coro::net

#include <coro/coro.hpp>

auto main() -> int
{
    auto make_tcp_echo_server = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            std::string buf(1024, '\0');

            while (true)
            {
                // Wait for data to be available to read.
                co_await client.poll(coro::poll_op::read);
                auto [rstatus, rspan] = client.recv(buf);
                if (rstatus.is_ok()) {
                    co_await client.poll(coro::poll_op::write);
                    client.send(std::span<const char>{rspan});
                } else if (rstatus.is_closed()) {
                    co_return;
                }
            }
        };

        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler, {"127.0.0.1", 8080}};

        while (true)
        {
            // Wait for a new connection.
            auto pstatus = co_await server.poll();
            switch (pstatus)
            {
                case coro::poll_status::read:
                {
                    auto client = server.accept_now();
                    if (client.socket().is_valid())
                    {
                        scheduler->spawn_detached(make_on_connection_task(std::move(client)));
                    } // else report error or something if the socket was invalid or could not be accepted.
                }
                break;
                case coro::poll_status::write:
                case coro::poll_status::error:
                case coro::poll_status::closed:
                case coro::poll_status::timeout:
                default:
                    co_return;
            }
        }

        co_return;
    };

    std::vector<std::unique_ptr<coro::scheduler>> schedulers;
    std::vector<coro::task<void>>                    workers{};

    const std::size_t count = std::thread::hardware_concurrency();

    schedulers.reserve(count);
    workers.reserve(count);

    for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        auto& scheduler = schedulers.emplace_back(coro::scheduler::make_unique(coro::scheduler::options{
            .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_inline}));

        workers.emplace_back(make_tcp_echo_server(scheduler));
    }

    coro::sync_wait(coro::when_all(std::move(workers)));
}

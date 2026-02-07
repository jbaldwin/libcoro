#include <coro/coro.hpp>

auto main() -> int
{
    auto make_http_200_ok_server = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
            std::string buf(1024, '\0');

            while (true)
            {
                auto [rstatus, rspan] = co_await client.read_some(buf);
                if (rstatus.is_ok())
                {
                    co_await client.write_some(response);
                }
                else
                {
                    co_return;
                }
            }
        };

        coro::net::tcp::server server{scheduler, {"127.0.0.1", 8888}};

        while (true)
        {
            auto client = co_await server.accept();
            if (client)
            {
                scheduler->spawn_detached(make_on_connection_task(std::move(*client)));
            }
            else
            {
                std::cerr << client.error().message();
                co_return;
            }
        }

        co_return;
    };

    std::vector<std::unique_ptr<coro::scheduler>> schedulers{};
    std::vector<coro::task<void>>                 workers{};

    const std::size_t count = std::thread::hardware_concurrency();

    schedulers.reserve(count);
    workers.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        auto& scheduler = schedulers.emplace_back(
            coro::scheduler::make_unique(
                coro::scheduler::options{
                    .execution_strategy = coro::scheduler::execution_strategy_t::process_tasks_inline}));

        workers.emplace_back(scheduler->schedule(make_http_200_ok_server(scheduler)));
    }

    coro::sync_wait(coro::when_all(std::move(workers)));
}

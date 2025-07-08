#include <coro/coro.hpp>

auto main() -> int
{
    auto make_tcp_echo_server = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            std::string buf(1024, '\0');

            while (true)
            {
                auto [rstatus, rspan] = co_await client.read(buf);
                switch (rstatus)
                {
                    case coro::net::read_status::ok:
                        co_await client.write(rspan);
                        break;
                    case coro::net::read_status::closed:
                    default:
                        co_return;
                }
            }
        };

        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler, coro::net::tcp::server::options{.port = 8888}};

        while (true)
        {
            auto client = co_await server.accept_client();
            if (client && client->socket().is_valid())
            {
                scheduler->spawn(make_on_connection_task(std::move(*client)));
            }
            else
            {
                co_return;
            }
        }

        co_return;
    };

    std::vector<coro::task<void>> workers{};
    for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        auto scheduler = coro::io_scheduler::make_shared(
            coro::io_scheduler::options{
                .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});

        workers.push_back(make_tcp_echo_server(scheduler));
    }

    coro::sync_wait(coro::when_all(std::move(workers)));
}

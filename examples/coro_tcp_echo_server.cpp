#include <coro/coro.hpp>

auto main() -> int
{
    auto make_tcp_echo_server = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp_client client) -> coro::task<void>
        {
            // Echo an basic http response or `buf` if you just want a true echo server.
            // Using this allows us to use other http benchmarking tools other than `ab`.
            std::string response = R"(
HTTP/1.1 200 OK
Content-Length: 0
Connection: keep-alive

)";

            std::string buf(1024, '\0');

            while (true)
            {
                co_await client.poll(coro::poll_op::read);
                const auto [rstatus, rspan] = client.recv(buf);
                if (rstatus == coro::net::recv_status::closed)
                {
                    co_return;
                }
                client.send(std::span<const char>{response});
            }
        };

        co_await scheduler->schedule();
        coro::net::tcp_server server{scheduler, coro::net::tcp_server::options{.port = 8888}};

        while (true)
        {
            // Wait for a new connection.
            auto pstatus = co_await server.poll();
            if (pstatus == coro::poll_status::event)
            {
                auto client = server.accept();
                if (client.socket().is_valid())
                {
                    scheduler->schedule(make_on_connection_task(std::move(client)));
                }
            }
        }

        co_return;
    };

    std::vector<coro::task<void>> workers{};
    for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        auto scheduler = std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});

        workers.push_back(make_tcp_echo_server(scheduler));
    }

    coro::sync_wait(coro::when_all(std::move(workers)));
}
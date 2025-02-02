#include <coro/coro.hpp>

auto main() -> int
{
    auto make_http_200_ok_server = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp::client client) -> coro::task<void>
        {
            std::string response =
                R"(HTTP/1.1 200 OK
Content-Length: 0
Connection: keep-alive

)";
            std::string buf(1024, '\0');

            while (true)
            {
                // Wait for data to be available to read.
                co_await client.poll(coro::poll_op::read);
                auto [rstatus, rspan] = client.recv(buf);
                switch (rstatus)
                {
                    case coro::net::recv_status::ok:
                        // Make sure the client socket can be written to.
                        co_await client.poll(coro::poll_op::write);
                        client.send(std::span<const char>{response});
                        break;
                    case coro::net::recv_status::would_block:
                        break;
                    case coro::net::recv_status::closed:
                    default:
                        co_return;
                }
            }
        };

        co_await scheduler->schedule();
        coro::net::tcp::server server{scheduler, coro::net::tcp::server::options{.port = 8888}};

        while (true)
        {
            // Wait for a new connection.
            auto pstatus = co_await server.poll();
            switch (pstatus)
            {
                case coro::poll_status::event:
                {
                    auto client = server.accept();
                    if (client.socket().is_valid())
                    {
                        scheduler->spawn(make_on_connection_task(std::move(client)));
                    } // else report error or something if the socket was invalid or could not be accepted.
                }
                break;
                case coro::poll_status::error:
                case coro::poll_status::closed:
                case coro::poll_status::timeout:
                default:
                    co_return;
            }
        }

        co_return;
    };

    std::vector<coro::task<void>> workers{};
    for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        auto scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
            .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline});

        workers.push_back(make_http_200_ok_server(scheduler));
    }

    coro::sync_wait(coro::when_all(std::move(workers)));
}

#include "coro/ranges/join.hpp"
#include <catch_amalgamated.hpp>
#include <coro/coro.hpp>
#include <coro/ranges/socket_stream.hpp>
#include <coro/ranges/take_until.hpp>
#include <coro/ranges/to.hpp>
#include <iostream>

TEST_CASE("", "[async_ranges]")
{
    auto scheduler = coro::scheduler::make_unique();

    auto server_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        co_await scheduler->schedule();

        auto server = coro::net::tcp::server{scheduler, {"127.0.0.1", 8080}};

        auto client = co_await server.accept();
        std::cerr << "server: accepted client\n";

        if (not client)
        {
            throw std::runtime_error(client.error().message());
        }

        std::string data{"Hello!\0 Hidden"};
        co_await client->write_all(data);
        std::cerr << "server: sent message\n";
    };
    auto client_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        co_await scheduler->schedule();

        auto client = coro::net::tcp::client{scheduler, {"127.0.0.1", 8080}};

        co_await client.connect();
        std::cerr << "client: connected\n";

        auto result = co_await (
            coro::ranges::to_chunked_stream(client) | coro::ranges::join() |
            coro::ranges::take_until([](auto f) -> bool { return static_cast<char>(f) == '0'; }) |
            coro::ranges::to<std::vector<std::byte>>);

        std::cerr << "client: received data\n";

        for (auto&& b : result)
        {
            std::cerr << static_cast<int>(b) << ' ';
        }
    };

    coro::sync_wait(coro::when_all(server_task(scheduler), client_task(scheduler)));
}
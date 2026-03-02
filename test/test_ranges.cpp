#include "coro/ranges/join.hpp"
#include "coro/ranges/transform.hpp"
#include "net/catch_net_extensions.hpp"
#include <catch_amalgamated.hpp>
#include <coro/coro.hpp>
#include <coro/ranges/socket_stream.hpp>
#include <coro/ranges/take_until.hpp>
#include <coro/ranges/to.hpp>
#include <iostream>

auto make_server_task(
    std::unique_ptr<coro::scheduler>& scheduler, const coro::net::socket_address& address, std::string_view message)
    -> coro::task<void>
{
    co_await scheduler->schedule();

    auto server = coro::net::tcp::server(scheduler, address);

    auto client = co_await server.accept();
    REQUIRE(client);
    std::cerr << "server: accepted\n";

    auto [wstatus, written] = co_await client->write_all(message);
    REQUIRE_THAT(wstatus, IsOk());
    std::cerr << "server: sent message\n";
}

auto make_client_stream(std::unique_ptr<coro::scheduler>& scheduler, const coro::net::socket_address& address)
    -> coro::task<coro::ranges::socket_stream<coro::net::tcp::client, std::vector<std::byte>>>
{
    co_await scheduler->schedule();
    auto client = coro::net::tcp::client{scheduler, address};

    auto cstatus = co_await client.connect();
    REQUIRE(cstatus == coro::net::connect_status::connected);
    std::cerr << "client: connected\n";

    co_return coro::ranges::to_chunked_stream(std::move(client));
}

TEST_CASE("Stream to string", "[async_ranges]")
{
    static auto address = coro::net::socket_address{"127.0.0.1", 8080};

    auto [message, expected] = GENERATE(table<std::string_view, std::string_view>({{"Hello world!", "Hello world!"}}));

    auto scheduler = coro::scheduler::make_unique();

    auto server = make_server_task(scheduler, address, message);

    auto client = [](std::unique_ptr<coro::scheduler>& scheduler, std::string_view message) -> coro::task<void>
    {
        co_await scheduler->schedule();
        auto stream = co_await make_client_stream(scheduler, address);

        auto pipeline = std::move(stream) | coro::ranges::join |
                        coro::ranges::transform([](auto byte) { return static_cast<char>(byte); }) |
                        coro::ranges::to<std::string>;

        auto result = co_await pipeline;
        CHECK(result == message);
    }(scheduler, expected);

    coro::sync_wait(coro::when_all(std::move(server), std::move(client)));
}
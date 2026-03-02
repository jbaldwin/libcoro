#include "coro/ranges/join.hpp"
#include "coro/ranges/take_until.hpp"
#include "coro/ranges/transform.hpp"
#include "net/catch_net_extensions.hpp"
#include <catch_amalgamated.hpp>
#include <coro/coro.hpp>
#include <coro/ranges/socket_stream.hpp>
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

using socket_stream = coro::ranges::socket_stream<coro::net::tcp::client, std::vector<std::byte>>;

auto make_client_stream(std::unique_ptr<coro::scheduler>& scheduler, const coro::net::socket_address& address)
    -> coro::task<socket_stream>
{
    co_await scheduler->schedule();
    auto client = coro::net::tcp::client{scheduler, address};

    auto cstatus = co_await client.connect();
    REQUIRE(cstatus == coro::net::connect_status::connected);
    std::cerr << "client: connected\n";

    co_return coro::ranges::to_chunked_stream(std::move(client));
}

// Helper adaptors
constexpr auto as_chars   = coro::ranges::transform([](auto byte) { return static_cast<char>(byte); });
constexpr auto until_zero = coro::ranges::take_until([](auto byte) { return static_cast<int>(byte) == 0; });
constexpr auto only_upper =
    coro::ranges::transform([](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; });

TEST_CASE("Stream to string", "[async_ranges]")
{
    static auto address = coro::net::socket_address{"127.0.0.1", 8080};

    // clang-format off
    auto [name, message, expected, pipeline_func] = GENERATE(
        table<
            std::string_view,
            std::string_view,
            std::string_view,
            std::function<coro::task<std::string>(socket_stream ss)>>(
            {
                {"Simple pipe",
                 "Hello world!",
                 "Hello world!",
                 [](auto ss) -> coro::task<std::string>
                 { return std::move(ss) | coro::ranges::join | as_chars | coro::ranges::to<std::string>; }},

                {"Empty stream",
                 "",
                 "",
                 [](auto ss) -> coro::task<std::string>
                 { return std::move(ss) | coro::ranges::join | as_chars | coro::ranges::to<std::string>; }},

                {"Stop at null",
                 "Part 1\0Part 2",
                 "Part 1",
                 [](auto ss) -> coro::task<std::string>
                 {
                     return std::move(ss) | coro::ranges::join | as_chars
                            | until_zero
                            | coro::ranges::to<std::string>;
                 }},

                {"Transformation (Uppercase)",
                 "libcoro",
                 "LIBCORO",
                 [](auto ss) -> coro::task<std::string>
                 {
                     return std::move(ss) | coro::ranges::join | as_chars
                            | only_upper
                            | coro::ranges::to<std::string>;
                 }}
            }));
    // clang-format on

    auto scheduler = coro::scheduler::make_unique();

    DYNAMIC_SECTION(name)
    {
        auto server = make_server_task(scheduler, address, message);

        auto client = [&](std::unique_ptr<coro::scheduler>& sched) -> coro::task<void>
        {
            co_await sched->schedule();

            auto stream = co_await make_client_stream(sched, address);

            std::string result = co_await pipeline_func(std::move(stream));

            CHECK(result == expected);
        }(scheduler);

        coro::sync_wait(coro::when_all(std::move(server), std::move(client)));
    }
}
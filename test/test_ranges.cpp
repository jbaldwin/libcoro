#include "catch_extensions.hpp"
#include "coro/ranges/await.hpp"
#include "coro/ranges/drain.hpp"
#include "coro/ranges/join.hpp"
#include "coro/ranges/take_until.hpp"
#include "coro/ranges/transform.hpp"
#include "net/catch_net_extensions.hpp"
#include <catch_amalgamated.hpp>
#include <coro/coro.hpp>
#include <coro/ranges/socket_stream.hpp>
#include <coro/ranges/to.hpp>
#include <iostream>

#ifdef LIBCORO_FEATURE_NETWORKING
auto make_server_task(
    std::unique_ptr<coro::scheduler>& scheduler, const coro::net::socket_address& address, std::string_view message)
    -> coro::task<void>
{
    co_await scheduler->schedule();

    auto server = coro::net::tcp::server(scheduler, address);

    auto client = co_await server.accept();
    REQUIRE_THREAD_SAFE(client);
    std::cerr << "server: accepted\n";

    auto [wstatus, written] = co_await client->write_all(message);
    REQUIRE_THAT_THREAD_SAFE(wstatus, IsOk());
    std::cerr << "server: sent message\n";

    co_await scheduler->yield_for(std::chrono::milliseconds(50)); // wait a bit until client reads data
}

using socket_stream = coro::ranges::socket_stream<coro::net::tcp::client, std::vector<std::byte>>;

auto make_client_stream(std::unique_ptr<coro::scheduler>& scheduler, const coro::net::socket_address& address)
    -> coro::task<socket_stream>
{
    co_await scheduler->schedule();
    auto client = coro::net::tcp::client{scheduler, address};

    co_await scheduler->yield_for(std::chrono::milliseconds(20)); // wait a bit until server wakes up

    auto cstatus = co_await client.connect();
    REQUIRE_THREAD_SAFE(cstatus == coro::net::connect_status::connected);
    std::cerr << "client: connected\n";

    co_return std::move(client) | coro::ranges::with_buffer();
}

static auto local_address = coro::net::socket_address{"127.0.0.1", 8080};

// Helper adaptors
constexpr auto as_chars   = coro::ranges::transform([](auto byte) { return static_cast<char>(byte); });
constexpr auto until_zero = coro::ranges::take_until([](auto byte) { return static_cast<int>(byte) == 0; });
constexpr auto only_upper =
    coro::ranges::transform([](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; });

TEST_CASE("Stream to string", "[async_ranges]")
{
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
        std::cerr << "--- starting " << name << " ---\n";
        auto server = make_server_task(scheduler, local_address, message);

        auto client = [](auto&& sched, auto&& pipe, auto&& expectd) -> coro::task<void>
        {
            co_await sched->schedule();

            auto stream = co_await make_client_stream(sched, local_address);

            std::string result = co_await pipe(std::move(stream));

            CHECK_THREAD_SAFE(result == expectd);
        }(scheduler, pipeline_func, expected);

        coro::sync_wait(coro::when_all(std::move(server), std::move(client)));
        std::cerr << "--- ending " << name << " ---\n";
    }
}

TEST_CASE("Sync stream to TCP pipeline", "[async_ranges]")
{
    auto                          scheduler = coro::scheduler::make_unique();
    std::vector<std::string_view> messages  = {"Hello", "world!"};

    auto server_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                          std::span<std::string_view>       msgs) -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp::server server{scheduler, local_address};
        auto                   client = co_await server.accept();

        REQUIRE(client);

        // clang-format off
        co_await (
            msgs // Sync stream
            | coro::ranges::transform([&](std::string_view msg) -> coro::task<void> {
                                            co_await client->write_all(msg);
                                        }) // Creating tasks
            | coro::ranges::await // co_awaiting them
            | coro::ranges::drain // Making the whole pipe a single task
        );
        // clang-format on

        // let the client catch up (BSD only)
        co_await scheduler->yield_for(std::chrono::milliseconds{40});
    }(scheduler, messages);

    auto client_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp::client client{scheduler, local_address};
        auto                   connect_status = co_await client.connect();
        REQUIRE(connect_status == coro::net::connect_status::connected);

        std::string result;

        SECTION("Dynamic buffer")
        {
            // clang-format off
            result = co_await (
                client
                | coro::ranges::with_buffer()
                | coro::ranges::join
                | as_chars
                | coro::ranges::to<std::string>
            );
            // clang-format on
        }

        SECTION("Custom buffer")
        {
            std::array<char, 4096> buffer{};
            // clang-format off
            result = co_await (
                client
                | coro::ranges::with_buffer(std::as_writable_bytes(std::span{buffer}))
                | coro::ranges::join
                | as_chars
                | coro::ranges::to<std::string>
            );
            // clang-format on
        }

        CHECK(result == "Helloworld!");
    }(scheduler);

    coro::sync_wait(coro::when_all(std::move(server_task), std::move(client_task)));
}

TEST_CASE("Partial reading", "[async_ranges]")
{
    auto scheduler = coro::scheduler::make_unique();

    char msg1[]{"Hello"};
    char msg2[]{"world!"};

    std::vector<std::span<char>> messages{std::span{msg1}, std::span{msg2}};

    auto server_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                          std::span<std::span<char>>        msgs) -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp::server server{scheduler, local_address};
        auto                   client = co_await server.accept();

        REQUIRE(client);

        // clang-format off
        auto pipe = msgs
            | coro::ranges::transform([&](auto &&msg) -> coro::task<void> {
                                          co_await client->write_all(msg);
                                      })
            | coro::ranges::await
            | coro::ranges::drain;

        co_await std::move(pipe);
        // clang-format on
    }(scheduler, messages);

    auto client_task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        co_await scheduler->schedule();

        coro::net::tcp::client client{scheduler, local_address};
        auto                   connect_status = co_await client.connect();
        REQUIRE(connect_status == coro::net::connect_status::connected);

        // clang-format off
        auto buffered_stream =
            client // tcp::client
            | coro::ranges::with_buffer(4096) // buffered reading
            | coro::ranges::join; // byte by byte

        auto get_word = buffered_stream
            | as_chars
            | until_zero;

         std::string hello = co_await (get_word | coro::ranges::to<std::string>);
         CHECK(hello == "Hello");

         std::string world = co_await (get_word | coro::ranges::to<std::string>);
         CHECK(world == "world!");

         std::cout << "client: got " << hello << ' ' << world << '\n';
        // clang-format on
    }(scheduler);

    coro::sync_wait(coro::when_all(std::move(server_task), std::move(client_task)));
}

#endif
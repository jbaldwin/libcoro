#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("sync_wait simple integer return", "[sync_wait]")
{
    auto func = []() -> coro::task<int> { co_return 11; };

    auto result = coro::sync_wait(func());
    REQUIRE(result == 11);
}

TEST_CASE("sync_wait void", "[sync_wait]")
{
    std::string output;

    auto func = [&]() -> coro::task<void>
    {
        output = "hello from sync_wait<void>\n";
        co_return;
    };

    coro::sync_wait(func());
    REQUIRE(output == "hello from sync_wait<void>\n");
}

TEST_CASE("sync_wait task co_await single", "[sync_wait]")
{
    auto answer = []() -> coro::task<int>
    {
        std::cerr << "\tThinking deep thoughts...\n";
        co_return 42;
    };

    auto await_answer = [&]() -> coro::task<int>
    {
        std::cerr << "\tStarting to wait for answer.\n";
        auto a = answer();
        std::cerr << "\tGot the coroutine, getting the value.\n";
        auto v = co_await a;
        std::cerr << "\tCoroutine value is " << v << "\n";
        REQUIRE(v == 42);
        v = co_await a;
        std::cerr << "\tValue is still " << v << "\n";
        REQUIRE(v == 42);
        co_return 1337;
    };

    auto output = coro::sync_wait(await_answer());
    REQUIRE(output == 1337);
}

TEST_CASE("sync_wait task that throws", "[sync_wait]")
{
    auto f = []() -> coro::task<uint64_t>
    {
        throw std::runtime_error("I always throw!");
        co_return 1;
    };

    REQUIRE_THROWS(coro::sync_wait(f()));
}

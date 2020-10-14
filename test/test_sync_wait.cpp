#include "catch.hpp"

#include <coro/coro.hpp>

TEST_CASE("sync_wait task multiple suspends return integer with sync_wait")
{
    auto func = []() -> coro::task<int> {
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_return 11;
    };

    auto result = coro::sync_wait(func());
    REQUIRE(result == 11);
}

TEST_CASE("sync_wait task co_await single")
{
    auto answer = []() -> coro::task<int> {
        std::cerr << "\tThinking deep thoughts...\n";
        co_return 42;
    };

    auto await_answer = [&]() -> coro::task<int> {
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

TEST_CASE("sync_wait_all accumulate")
{
    std::atomic<uint64_t> counter{0};
    auto                  func = [&](uint64_t amount) -> coro::task<void> {
        std::cerr << "amount=" << amount << "\n";
        counter += amount;
        co_return;
    };

    coro::sync_wait_all(func(100), func(10), func(50));

    REQUIRE(counter == 160);
}

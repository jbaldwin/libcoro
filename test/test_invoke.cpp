#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>
#include <iostream>

TEST_CASE("invoke", "[invoke]")
{
    std::cerr << "[invoke]\n\n";
}

TEST_CASE("invoke no captures", "[invoke]")
{
    int a = 1;
    int b = 2;
    auto make_task = [](int a, int b) -> coro::task<int> { co_return a + b; };
    auto task = coro::invoke(make_task, a, b);

    REQUIRE(coro::sync_wait(task) == 3);
}

TEST_CASE("invoke captures", "[invoke]")
{
    int a = 1;
    int b = 2;
    int c = 3;
    auto make_task = [&c](int a, int b) -> coro::task<int> { co_return a + b + c; };
    auto task = coro::invoke(make_task, a, b);

    REQUIRE(coro::sync_wait(task) == 6);
}

TEST_CASE("invoke captures with suspend", "[invoke]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    int a = 1;
    int b = 2;
    int c = 3;
    auto make_task = [&tp, &c](int a, int b) -> coro::task<int> {
        co_await tp->schedule();
        co_return a + b + c;
    };
    auto task = coro::invoke(make_task, a, b);

    REQUIRE(coro::sync_wait(task) == 6);
}

TEST_CASE("~invoke", "[invoke]")
{
    std::cerr << "[~invoke]\n\n";
}

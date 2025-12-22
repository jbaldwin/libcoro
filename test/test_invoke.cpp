#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>
#include <iostream>

TEST_CASE("invoke", "[invoke]")
{
    std::cerr << "[invoke]\n\n";
}

TEST_CASE("invoke no captures", "[invoke]")
{
    coro::task<int> task;
    coro::event suspended{}; // capture by ref since its on stable stack frame location for this test

    {
        int a = 1;
        int b = 2;
        auto make_task = [&suspended](int a, int b) -> coro::task<int>
        {
            co_await suspended;
            co_return a + b;
        };
        task = coro::invoke(make_task, a, b);
        task.resume(); // Start the task up to the first suspension point.
        // Let the argument variables go out of scope to guarantee they are destroyed.
    }

    // Resume the task, it should have copied all variables to its own coroutine frame.
    suspended.set();
    REQUIRE(coro::sync_wait(task) == 3);
}

TEST_CASE("invoke captures", "[invoke]")
{
    coro::task<int> task;
    coro::event suspended{};

    {
        int a = 1;
        int b = 2;
        int c = 3;
        int d = 4;
        auto make_task = [&suspended, c, d](int a, int b) -> coro::task<int>
        {
            co_await suspended;
            co_return a + b + c + d;
        };
        task = coro::invoke(make_task, a, b);
        task.resume();
    }

    suspended.set();
    REQUIRE(coro::sync_wait(task) == 10);
}

TEST_CASE("~invoke", "[invoke]")
{
    std::cerr << "[~invoke]\n\n";
}

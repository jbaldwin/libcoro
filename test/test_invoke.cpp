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

    {
        int a = 1;
        int b = 2;
        auto make_task = [](int a, int b) -> coro::task<int> { co_return a + b; };
        task = coro::invoke(make_task, a, b);
        // Let the argument variables go out of scope to guarantee they are destroyed on this local scope.
    }

    REQUIRE(coro::sync_wait(task) == 3);
}

TEST_CASE("invoke captures", "[invoke]")
{
    coro::task<int> task;

    {
        int a = 1;
        int b = 2;
        int c = 3;
        int d = 4;
        auto make_task = [c, d](int a, int b) -> coro::task<int>
        {
            co_return a + b + c + d;
        };
        task = coro::invoke(make_task, a, b);
    }

    REQUIRE(coro::sync_wait(task) == 10);
}

TEST_CASE("invoke captures in coroutine context", "[invoke]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::task<int> task;

    auto make_root_task = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<int>
    {
        co_await tp->schedule();

        coro::task<int> user_task;
        {
            int a = 1;
            int b = 2;
            int c = 3;
            int d = 4;
            auto make_user_task = [c, d](int a, int b) -> coro::task<int>
            {
                co_return a + b + c + d;
            };
            user_task = coro::invoke(make_user_task, a, b);
        }

        co_return co_await user_task;
    };

    REQUIRE(coro::sync_wait(make_root_task(tp)) == 10);
}

TEST_CASE("invoke co_await coro::invoke", "[invoke]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::task<int> task;

    auto make_root_task = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<int>
    {
        co_await tp->schedule();

        int a = 1;
        int b = 2;
        int c = 3;
        int d = 4;
        auto make_user_task = [c, d](int a, int b) -> coro::task<int>
        {
            co_return a + b + c + d;
        };
        co_return co_await coro::invoke(make_user_task, a, b);
    };

    REQUIRE(coro::sync_wait(make_root_task(tp)) == 10);
}

TEST_CASE("~invoke", "[invoke]")
{
    std::cerr << "[~invoke]\n\n";
}

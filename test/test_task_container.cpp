#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <atomic>
#include <iostream>

TEST_CASE("task_container", "[task_container]")
{
    std::cerr << "[task_container]\n\n";
}

using namespace std::chrono_literals;

TEST_CASE("task_container schedule single task", "[task_container]")
{
    auto                    s = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::event             can_start{false};
    int                     value = 0;
    auto make_task = [](int& value, coro::event& can_start) -> coro::task<void>
    {
        co_await can_start;
        value = 37;
        co_return;
    };

    coro::task_container<coro::thread_pool> tc{s};
    tc.start(make_task(value, can_start));
    REQUIRE(tc.size() == 1);
    // Only allow the task to complete once we have checked the container's size,
    // otherwise the require could "spuriously" fail due to the task already being done.
    can_start.set(s);
    coro::sync_wait(tc.yield_until_empty());
    REQUIRE(value == 37);
    REQUIRE(tc.empty());
}

TEST_CASE("task_container submit mutiple tasks", "[task_container]")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    auto s = coro::thread_pool::make_unique(
        coro::thread_pool::options{.thread_count = 1});
    coro::task_container<coro::thread_pool> tc{s};

    auto make_task = [](std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        counter++;
        co_return;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        tc.start(make_task(counter));
    }

    coro::sync_wait(tc.yield_until_empty());
    REQUIRE(counter == n);
    REQUIRE(tc.empty());
}

TEST_CASE("~task_container", "[task_container]")
{
    std::cerr << "[~task_container]\n\n";
}

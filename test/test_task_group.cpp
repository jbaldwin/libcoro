#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <atomic>
#include <iostream>

TEST_CASE("task_group", "[task_group]")
{
    std::cerr << "[task_group]\n\n";
}

using namespace std::chrono_literals;

TEST_CASE("task_group schedule single task", "[task_group]")
{
    auto        s = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::event can_start{false};
    int         value     = 0;
    auto        make_task = [](int& value, coro::event& can_start) -> coro::task<void>
    {
        co_await can_start;
        value = 37;
        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.emplace_back(make_task(value, can_start));
    coro::task_group<coro::thread_pool> tc{s, std::move(tasks)};
    REQUIRE(tc.size() == 1);
    // Only allow the task to complete once we have checked the container's size,
    // otherwise the REQUIRE could "spuriously" fail due to the task already being done.
    can_start.set(s);
    coro::sync_wait(tc);
    REQUIRE(value == 37);
    REQUIRE(tc.empty());
}

TEST_CASE("task_group submit multiple tasks", "[task_group]")
{
    constexpr std::size_t         n = 1000;
    std::atomic<uint64_t>         counter{0};
    auto                          tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(n);

    auto make_task = [](std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        ++counter;
        co_return;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        tasks.emplace_back(make_task(counter));
    }
    coro::task_group<coro::thread_pool> group{tp, std::move(tasks)};

    coro::sync_wait(group);
    REQUIRE(group.empty());
    REQUIRE(counter == n);
}

TEST_CASE("~task_group", "[task_group]")
{
    std::cerr << "[~task_group]\n\n";
}

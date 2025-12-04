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

    coro::task_group<coro::thread_pool> tc{s, make_task(value, can_start)};
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

TEST_CASE("task_group start multiple tasks", "[task_group]")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    auto                  tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::latch           all_started{n};

    auto make_task = [](coro::latch& all_started, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        all_started.count_down();
        ++counter;
        co_return;
    };

    coro::task_group<coro::thread_pool> group{tp};
    for (std::size_t i = 0; i < n; ++i)
    {
        (void)group.start(make_task(all_started, counter));
    }

    // this is necessary since tasks do not all start so the task group counter can drop to zero before all tasks
    // have even started, this guarantees the event we get after all have started truly denotes all tasks are complete.
    coro::sync_wait(all_started);
    coro::sync_wait(group);
    REQUIRE(group.empty());
    REQUIRE(counter.load() == n);
}

TEST_CASE("task_group empty multiple times with event", "[task_group]")
{
    auto        tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    coro::event e1;
    coro::event e2;

    auto make_task = [](coro::event& e) -> coro::task<void>
    {
        co_await e;
        co_return;
    };

    coro::task_group group{tp, make_task(e1)};
    REQUIRE(group.size() == 1);
    e1.set();
    coro::sync_wait(group);
    REQUIRE(group.empty());

    // This will reset the event by starting a new task.
    (void)group.start(make_task(e2));
    REQUIRE(group.size() == 1);
    e2.set();
    coro::sync_wait(group);
    REQUIRE(group.empty());
}

TEST_CASE("~task_group", "[task_group]")
{
    std::cerr << "[~task_group]\n\n";
}

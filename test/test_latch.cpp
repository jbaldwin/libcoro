#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("latch count=0", "[latch]")
{
    coro::latch l{0};

    auto make_task = [](coro::latch& l) -> coro::task<uint64_t>
    {
        co_await l;
        co_return 42;
    };

    auto task = make_task(l);

    task.resume();
    REQUIRE(task.is_ready()); // The latch never waits due to zero count.
    REQUIRE(task.promise().result() == 42);
}

TEST_CASE("latch count=1", "[latch]")
{
    coro::latch l{1};

    auto make_task = [](coro::latch& l) -> coro::task<uint64_t>
    {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task(l);

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down();
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 1);
}

TEST_CASE("latch count=1 count_down=5", "[latch]")
{
    coro::latch l{1};

    auto make_task = [](coro::latch& l) -> coro::task<uint64_t>
    {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task(l);

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down(5);
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 1);
}

TEST_CASE("latch count=5 count_down=1 x5", "[latch]")
{
    coro::latch l{5};

    auto make_task = [](coro::latch& l) -> coro::task<uint64_t>
    {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task(l);

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down(1);
    REQUIRE_FALSE(task.is_ready());
    l.count_down(1);
    REQUIRE_FALSE(task.is_ready());
    l.count_down(1);
    REQUIRE_FALSE(task.is_ready());
    l.count_down(1);
    REQUIRE_FALSE(task.is_ready());
    l.count_down(1);
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 5);
}

TEST_CASE("latch count=5 count_down=5", "[latch]")
{
    coro::latch l{5};

    auto make_task = [](coro::latch& l) -> coro::task<uint64_t>
    {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task(l);

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down(5);
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 5);
}

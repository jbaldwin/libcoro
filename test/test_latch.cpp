#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("latch count=0")
{
    coro::latch l{0};

    auto task = [&]() -> coro::task<uint64_t> {
        co_await l;
        co_return 42;
    }();

    task.resume();
    REQUIRE(task.is_ready()); // The latch never waits due to zero count.
    REQUIRE(task.promise().return_value() == 42);
}

TEST_CASE("latch count=1")
{
    coro::latch l{1};

    auto task = [&]() -> coro::task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    }();

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down();
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().return_value() == 1);
}

TEST_CASE("latch count=1 count_down=5")
{
    coro::latch l{1};

    auto task = [&]() -> coro::task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    }();

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down(5);
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().return_value() == 1);
}

TEST_CASE("latch count=5 count_down=1 x5")
{
    coro::latch l{5};

    auto task = [&]() -> coro::task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    }();

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
    REQUIRE(task.promise().return_value() == 5);
}

TEST_CASE("latch count=5 count_down=5")
{
    coro::latch l{5};

    auto task = [&]() -> coro::task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    }();

    task.resume();
    REQUIRE_FALSE(task.is_ready());

    l.count_down(5);
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().return_value() == 5);
}

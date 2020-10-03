#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("event single awaiter")
{
    coro::event e{};

    auto func = [&]() -> coro::task<uint64_t> {
        co_await e;
        co_return 42;
    };

    auto task = func();

    task.resume();
    REQUIRE_FALSE(task.is_ready());
    e.set(); // this will automaticaly resume the task that is awaiting the event.
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().result() == 42);
}


auto producer(coro::event& event) -> void
{
    // Long running task that consumers are waiting for goes here...
    event.set();
}

auto consumer(const coro::event& event) -> coro::task<uint64_t>
{
    co_await event;
    // Normally consume from some object which has the stored result from the producer
    co_return 42;
}

TEST_CASE("event one watcher")
{
    coro::event e{};

    auto value = consumer(e);
    value.resume(); // start co_awaiting event
    REQUIRE_FALSE(value.is_ready());

    producer(e);

    REQUIRE(value.promise().result() == 42);
}

TEST_CASE("event multiple watchers")
{
    coro::event e{};

    auto value1 = consumer(e);
    auto value2 = consumer(e);
    auto value3 = consumer(e);
    value1.resume(); // start co_awaiting event
    value2.resume();
    value3.resume();
    REQUIRE_FALSE(value1.is_ready());
    REQUIRE_FALSE(value2.is_ready());
    REQUIRE_FALSE(value3.is_ready());

    producer(e);

    REQUIRE(value1.promise().result() == 42);
    REQUIRE(value2.promise().result() == 42);
    REQUIRE(value3.promise().result() == 42);
}

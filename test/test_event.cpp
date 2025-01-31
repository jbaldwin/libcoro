#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("event single awaiter", "[event]")
{
    coro::event e{};

    auto func = [](coro::event& e) -> coro::task<uint64_t>
    {
        co_await e;
        co_return 42;
    };

    auto task = func(e);

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

TEST_CASE("event one watcher", "[event]")
{
    coro::event e{};

    auto value = consumer(e);
    value.resume(); // start co_awaiting event
    REQUIRE_FALSE(value.is_ready());

    producer(e);

    REQUIRE(value.promise().result() == 42);
}

TEST_CASE("event multiple watchers", "[event]")
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

TEST_CASE("event reset", "[event]")
{
    coro::event e{};

    e.reset();
    REQUIRE_FALSE(e.is_set());

    auto value1 = consumer(e);
    value1.resume(); // start co_awaiting event
    REQUIRE_FALSE(value1.is_ready());

    producer(e);
    REQUIRE(value1.promise().result() == 42);

    e.reset();

    auto value2 = consumer(e);
    value2.resume();
    REQUIRE_FALSE(value2.is_ready());

    producer(e);

    REQUIRE(value2.promise().result() == 42);
}

TEST_CASE("event fifo", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter =
        [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter, uint64_t value) -> coro::task<void>
    {
        co_await tp.schedule();
        co_await e;

        counter++;
        REQUIRE(counter == value);

        co_return;
    };

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(
        make_waiter(tp, e, counter, 1),
        make_waiter(tp, e, counter, 2),
        make_waiter(tp, e, counter, 3),
        make_waiter(tp, e, counter, 4),
        make_waiter(tp, e, counter, 5),
        make_setter(tp, e, counter)));

    REQUIRE(counter == 5);
}

TEST_CASE("event fifo none", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_setter(tp, e, counter)));

    REQUIRE(counter == 0);
}

TEST_CASE("event fifo single", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter =
        [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter, uint64_t value) -> coro::task<void>
    {
        co_await tp.schedule();
        co_await e;

        counter++;
        REQUIRE(counter == value);

        co_return;
    };

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_waiter(tp, e, counter, 1), make_setter(tp, e, counter)));

    REQUIRE(counter == 1);
}

TEST_CASE("event fifo executor", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter =
        [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter, uint64_t value) -> coro::task<void>
    {
        co_await tp.schedule();
        co_await e;

        counter++;
        REQUIRE(counter == value);

        co_return;
    };

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(tp, coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(
        make_waiter(tp, e, counter, 1),
        make_waiter(tp, e, counter, 2),
        make_waiter(tp, e, counter, 3),
        make_waiter(tp, e, counter, 4),
        make_waiter(tp, e, counter, 5),
        make_setter(tp, e, counter)));

    REQUIRE(counter == 5);
}

TEST_CASE("event fifo none executor", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(tp, coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_setter(tp, e, counter)));

    REQUIRE(counter == 0);
}

TEST_CASE("event fifo single executor", "[event]")
{
    coro::event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter =
        [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter, uint64_t value) -> coro::task<void>
    {
        co_await tp.schedule();
        co_await e;

        counter++;
        REQUIRE(counter == value);

        co_return;
    };

    auto make_setter = [](coro::thread_pool& tp, coro::event& e, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();
        REQUIRE(counter == 0);
        e.set(tp, coro::resume_order_policy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_waiter(tp, e, counter, 1), make_setter(tp, e, counter)));

    REQUIRE(counter == 1);
}

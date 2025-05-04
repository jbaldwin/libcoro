#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

TEST_CASE("queue shutdown produce", "[queue]")
{
    coro::queue<uint64_t> q{};

    auto make_consumer_task = [](coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        auto expected = co_await q.pop();
        if (!expected)
        {
            co_return 0;
        }
        co_return std::move(*expected);
    };

    coro::sync_wait(q.shutdown_notify_waiters());
    coro::sync_wait(q.push(42));

    auto result = coro::sync_wait(make_consumer_task(q));
    REQUIRE(result == 0);
    REQUIRE(q.empty());
}

TEST_CASE("queue single produce consume", "[queue]")
{
    coro::queue<uint64_t> q{};

    auto make_consumer_task = [](coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        auto expected = co_await q.pop();
        if (!expected)
        {
            co_return 0;
        }
        co_return std::move(*expected);
    };

    coro::sync_wait(q.push(42));

    auto result = coro::sync_wait(make_consumer_task(q));
    REQUIRE(result == 42);
    REQUIRE(q.empty());
}

TEST_CASE("queue multiple produce and consume", "[queue]")
{
    const uint64_t        ITERATIONS = 10;
    coro::queue<uint64_t> q{};

    auto make_consumer_task = [](coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        auto expected = co_await q.pop();
        if (!expected)
        {
            co_return 0;
        }
        co_return std::move(*expected);
    };

    std::vector<coro::task<uint64_t>> tasks{};
    for (uint64_t i = 0; i < ITERATIONS; ++i)
    {
        coro::sync_wait(q.push(i));
        tasks.emplace_back(make_consumer_task(q));
    }

    auto results = coro::sync_wait(coro::when_all(std::move(tasks)));
    for (uint64_t i = 0; i < ITERATIONS; ++i)
    {
        REQUIRE(results[i].return_value() == i);
    }
}

TEST_CASE("queue produce consume direct", "[queue]")
{
    const uint64_t        ITERATIONS = 10;
    coro::queue<uint64_t> q{};
    coro::thread_pool     tp{};

    auto make_producer_task = [&ITERATIONS](coro::thread_pool& tp, coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        co_await tp.schedule();
        for (uint64_t i = 0; i < ITERATIONS; ++i)
        {
            co_await q.push(i);
            co_await tp.yield();
        }

        co_await q.shutdown_notify_waiters_drain(tp);

        co_return 0;
    };

    auto make_consumer_task = [&ITERATIONS](coro::thread_pool& tp, coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        co_await tp.schedule();

        uint64_t sum{0};

        while (true)
        {
            auto expected = co_await q.pop();
            if (!expected)
            {
                co_return sum;
            }
            sum += *expected;
        }
    };

    auto results = coro::sync_wait(coro::when_all(make_consumer_task(tp, q), make_producer_task(tp, q)));
    REQUIRE(std::get<0>(results).return_value() == 45);
    REQUIRE(std::get<1>(results).return_value() == 0);
}

TEST_CASE("queue multithreaded produce consume", "[queue]")
{
    const uint64_t        WORKERS    = 3;
    const uint64_t        ITERATIONS = 100;
    coro::queue<uint64_t> q{};
    coro::thread_pool     tp{};
    std::atomic<uint64_t> counter{0};
    coro::latch           wait{WORKERS};

    auto make_producer_task =
        [&ITERATIONS](coro::thread_pool& tp, coro::queue<uint64_t>& q, coro::latch& w) -> coro::task<void>
    {
        co_await tp.schedule();
        for (uint64_t i = 0; i < ITERATIONS; ++i)
        {
            co_await q.push(i);
            co_await tp.yield();
        }

        w.count_down();
        co_return;
    };

    auto make_shutdown_task = [](coro::thread_pool& tp, coro::queue<uint64_t>& q, coro::latch& w) -> coro::task<void>
    {
        // Wait for all producers to complete.
        co_await w;

        // Wake up all waiters.
        co_await q.shutdown_notify_waiters_drain(tp);
    };

    auto make_consumer_task =
        [&ITERATIONS](
            coro::thread_pool& tp, coro::queue<uint64_t>& q, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await tp.schedule();

        while (true)
        {
            auto expected = co_await q.pop();
            if (!expected)
            {
                co_return;
            }
            counter += *expected;
        }
    };

    std::vector<coro::task<void>> tasks{};
    for (uint64_t i = 0; i < WORKERS; ++i)
    {
        tasks.emplace_back(make_producer_task(tp, q, wait));
        tasks.emplace_back(make_consumer_task(tp, q, counter));
    }
    tasks.emplace_back(make_shutdown_task(tp, q, wait));

    coro::sync_wait(coro::when_all(std::move(tasks)));
    REQUIRE(counter == 14850);
}

TEST_CASE("queue stopped", "[queue]")
{
    coro::queue<uint64_t> q{};

    auto make_consumer_task = [](coro::queue<uint64_t>& q) -> coro::task<uint64_t>
    {
        auto expected = co_await q.pop();
        if (!expected)
        {
            co_return 0;
        }
        co_return std::move(*expected);
    };

    coro::sync_wait(q.push(42));
    coro::sync_wait(q.shutdown_notify_waiters());

    auto result = coro::sync_wait(make_consumer_task(q));
    REQUIRE(result == 0);
    REQUIRE(q.size() == 1); // The item was not consumed due to shutdown.
}

#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

TEST_CASE("ring_buffer zero num_elements", "[ring_buffer]")
{
    REQUIRE_THROWS(coro::ring_buffer<uint64_t, 0>{});
}

TEST_CASE("ring_buffer single element", "[ring_buffer]")
{
    const size_t                   iterations = 10;
    coro::ring_buffer<uint64_t, 1> rb{};

    std::vector<uint64_t> output{};

    auto make_producer_task = [&]() -> coro::task<void>
    {
        for (size_t i = 1; i <= iterations; ++i)
        {
            std::cerr << "produce: " << i << "\n";
            co_await rb.produce(i);
        }
        co_return;
    };

    auto make_consumer_task = [&]() -> coro::task<void>
    {
        for (size_t i = 1; i <= iterations; ++i)
        {
            auto expected = co_await rb.consume();
            auto value    = std::move(*expected);

            std::cerr << "consume: " << value << "\n";
            output.emplace_back(std::move(value));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(make_producer_task(), make_consumer_task()));

    for (size_t i = 1; i <= iterations; ++i)
    {
        REQUIRE(output[i - 1] == i);
    }

    REQUIRE(rb.empty());
}

TEST_CASE("ring_buffer many elements many producers many consumers", "[ring_buffer]")
{
    const size_t iterations = 1'000'000;
    const size_t consumers  = 100;
    const size_t producers  = 100;

    coro::thread_pool               tp{coro::thread_pool::options{.thread_count = 4}};
    coro::ring_buffer<uint64_t, 64> rb{};

    auto make_producer_task = [&]() -> coro::task<void>
    {
        co_await tp.schedule();
        auto to_produce = iterations / producers;

        for (size_t i = 1; i <= to_produce; ++i)
        {
            co_await rb.produce(i);
        }

        // Wait for all the values to be consumed prior to shutting down the ring buffer.
        while (!rb.empty())
        {
            co_await tp.yield();
        }

        rb.notify_waiters(); // signal to all consumers (or even producers) we are done/shutting down.

        co_return;
    };

    auto make_consumer_task = [&]() -> coro::task<void>
    {
        co_await tp.schedule();

        while (true)
        {
            auto expected = co_await rb.consume();
            if (!expected)
            {
                break;
            }

            auto item = std::move(*expected);
            (void)item;

            co_await tp.yield(); // mimic some work
        }

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(consumers * producers);

    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task());
    }
    for (size_t i = 0; i < producers; ++i)
    {
        tasks.emplace_back(make_producer_task());
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(rb.empty());
}

TEST_CASE("ring_buffer producer consumer separate threads", "[ring_buffer]")
{
    // This test explicitly tests two independent threads producing and consuming from a ring_buffer.
    // Issue #120 reported a race condition when the ring buffer is accessed on separate threads.

    const size_t                   iterations = 10'000'000;
    coro::ring_buffer<uint64_t, 2> rb{};

    // We'll use an io schedule so we can use yield_for on shutdown since its two threads.
    coro::thread_pool producer_tp{coro::thread_pool::options{.thread_count = 1}};
    coro::thread_pool consumer_tp{coro::thread_pool::options{.thread_count = 1}};

    auto make_producer_task = [&]() -> coro::task<void>
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            // This test has to constantly reschedule onto the other thread since the produce
            // and consume calls will end up switching which "thread" is processing.
            co_await producer_tp.schedule();
            co_await rb.produce(i);
        }

        while (!rb.empty())
        {
            co_await producer_tp.yield();
        }

        rb.notify_waiters(); // Shut everything down.

        co_return;
    };

    auto make_consumer_task = [&]() -> coro::task<void>
    {
        while (true)
        {
            co_await consumer_tp.schedule();
            auto expected = co_await rb.consume();
            if (!expected)
            {
                break;
            }

            auto item = std::move(*expected);
            (void)item;

            co_await consumer_tp.yield(); // mimic some work
        }

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.emplace_back(make_producer_task());
    tasks.emplace_back(make_consumer_task());

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(rb.empty());
}

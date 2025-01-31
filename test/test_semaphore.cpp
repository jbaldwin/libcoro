#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

TEST_CASE("semaphore binary", "[semaphore]")
{
    std::vector<uint64_t> output;

    coro::semaphore s{1};

    auto make_emplace_task = [](coro::semaphore& s, std::vector<uint64_t>& output) -> coro::task<void>
    {
        std::cerr << "Acquiring semaphore\n";
        co_await s.acquire();
        REQUIRE_FALSE(s.try_acquire());
        std::cerr << "semaphore acquired, emplacing back 1\n";
        output.emplace_back(1);
        std::cerr << "coroutine done with resource, releasing\n";
        REQUIRE(s.value() == 0);
        s.release();

        REQUIRE(s.value() == 1);

        REQUIRE(s.try_acquire());
        s.release();

        co_return;
    };

    coro::sync_wait(make_emplace_task(s, output));

    REQUIRE(s.value() == 1);
    REQUIRE(s.try_acquire());
    REQUIRE(s.value() == 0);
    s.release();
    REQUIRE(s.value() == 1);

    REQUIRE(output.size() == 1);
    REQUIRE(output[0] == 1);
}

TEST_CASE("semaphore binary many waiters until event", "[semaphore]")
{
    std::atomic<uint64_t>         value{0};
    std::vector<coro::task<void>> tasks;

    coro::semaphore s{1}; // acquires and holds the semaphore until the event is triggered
    coro::event     e;    // triggers the blocking thread to release the semaphore

    auto make_task = [](coro::semaphore& s, std::atomic<uint64_t>& value, uint64_t id) -> coro::task<void>
    {
        std::cerr << "id = " << id << " waiting to acquire the semaphore\n";
        co_await s.acquire();

        // Should always be locked upon acquiring the semaphore.
        REQUIRE_FALSE(s.try_acquire());

        std::cerr << "id = " << id << " semaphore acquired\n";
        value.fetch_add(1, std::memory_order::relaxed);
        std::cerr << "id = " << id << " semaphore release\n";
        s.release();
        co_return;
    };

    auto make_block_task = [](coro::semaphore& s, coro::event& e) -> coro::task<void>
    {
        std::cerr << "block task acquiring lock\n";
        co_await s.acquire();
        REQUIRE_FALSE(s.try_acquire());
        std::cerr << "block task acquired semaphore, waiting on event\n";
        co_await e;
        std::cerr << "block task releasing semaphore\n";
        s.release();
        co_return;
    };

    auto make_set_task = [](coro::event& e) -> coro::task<void>
    {
        std::cerr << "set task setting event\n";
        e.set();
        co_return;
    };

    tasks.emplace_back(make_block_task(s, e));

    // Create N tasks that attempt to acquire the semaphore.
    for (uint64_t i = 1; i <= 4; ++i)
    {
        tasks.emplace_back(make_task(s, value, i));
    }

    tasks.emplace_back(make_set_task(e));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(value == 4);
}

TEST_CASE("semaphore ringbuffer", "[semaphore]")
{
    const std::size_t iterations = 10;

    // This test is run in the context of a thread pool so the producer task can yield.  Otherwise
    // the producer will just run wild!
    coro::thread_pool             tp{coro::thread_pool::options{.thread_count = 1}};
    std::atomic<uint64_t>         value{0};
    std::vector<coro::task<void>> tasks;

    coro::semaphore s{2, 2};

    auto make_consumer_task =
        [](coro::thread_pool& tp, coro::semaphore& s, std::atomic<uint64_t>& value, uint64_t id) -> coro::task<void>
    {
        co_await tp.schedule();

        while (true)
        {
            std::cerr << "id = " << id << " waiting to acquire the semaphore\n";
            auto result = co_await s.acquire();
            if (result == coro::semaphore::acquire_result::acquired)
            {
                std::cerr << "id = " << id << " semaphore acquired, consuming value\n";

                value.fetch_add(1, std::memory_order::release);
                // In the ringbfuffer acquire is 'consuming', we never release back into the buffer
            }
            else
            {
                std::cerr << "id = " << id << " exiting\n";
                break; // while
            }
        }

        co_return;
    };

    auto make_producer_task = [](coro::thread_pool& tp, coro::semaphore& s) -> coro::task<void>
    {
        co_await tp.schedule();

        for (size_t i = 2; i < iterations; ++i)
        {
            std::cerr << "producer: doing work\n";
            // Do some work...

            std::cerr << "producer: releasing\n";
            s.release();
            std::cerr << "producer: produced\n";
            co_await tp.yield();
        }

        std::cerr << "producer exiting\n";
        s.notify_waiters();
        co_return;
    };

    tasks.emplace_back(make_producer_task(tp, s));
    tasks.emplace_back(make_consumer_task(tp, s, value, 1));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(value == iterations);
}

TEST_CASE("semaphore ringbuffer many producers and consumers", "[semaphore]")
{
    const std::size_t consumers  = 16;
    const std::size_t producers  = 1;
    const std::size_t iterations = 100'000;

    std::atomic<uint64_t> value{0};

    coro::semaphore s{50, 0};

    coro::thread_pool tp{}; // let er rip

    auto make_consumer_task =
        [](coro::thread_pool& tp, coro::semaphore& s, std::atomic<uint64_t>& value, uint64_t id) -> coro::task<void>
    {
        co_await tp.schedule();

        while (true)
        {
            auto result = co_await s.acquire();
            if (result == coro::semaphore::acquire_result::acquired)
            {
                co_await tp.schedule();
                value.fetch_add(1, std::memory_order::relaxed);
            }
            else
            {
                std::cerr << "consumer " << id << " exiting\n";
                break; // while
            }
        }
        co_return;
    };

    auto make_producer_task =
        [](coro::thread_pool& tp, coro::semaphore& s, std::atomic<uint64_t>& value, uint64_t id) -> coro::task<void>
    {
        co_await tp.schedule();

        for (size_t i = 0; i < iterations; ++i)
        {
            s.release();
        }

        while (value.load(std::memory_order::relaxed) < iterations)
        {
            co_await tp.yield();
        }

        std::cerr << "producer " << id << " exiting\n";

        s.notify_waiters();

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task(tp, s, value, i));
    }
    for (size_t i = 0; i < producers; ++i)
    {
        tasks.emplace_back(make_producer_task(tp, s, value, i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(value >= iterations);
}

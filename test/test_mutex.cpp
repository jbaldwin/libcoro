#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <thread>

TEST_CASE("mutex single waiter not locked", "[mutex]")
{
    std::vector<uint64_t> output;

    coro::mutex m;

    auto make_emplace_task = [](coro::mutex& m, std::vector<uint64_t>& output) -> coro::task<void>
    {
        std::cerr << "Acquiring lock\n";
        {
            auto scoped_lock = co_await m.lock();
            REQUIRE_FALSE(m.try_lock());
            std::cerr << "lock acquired, emplacing back 1\n";
            output.emplace_back(1);
            std::cerr << "coroutine done\n";
        }

        // The scoped lock should release the lock upon destructing.
        REQUIRE(m.try_lock());
        REQUIRE_FALSE(m.try_lock());
        m.unlock();

        co_return;
    };

    coro::sync_wait(make_emplace_task(m, output));

    REQUIRE(m.try_lock());
    m.unlock();

    REQUIRE(output.size() == 1);
    REQUIRE(output[0] == 1);
}

TEST_CASE("mutex many waiters until event", "[mutex]")
{
    std::atomic<uint64_t>         value{0};
    std::vector<coro::task<void>> tasks;

    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    coro::mutex m; // acquires and holds the lock until the event is triggered
    coro::event e; // triggers the blocking thread to release the lock

    auto make_task =
        [](coro::thread_pool& tp, coro::mutex& m, std::atomic<uint64_t>& value, uint64_t id) -> coro::task<void>
    {
        co_await tp.schedule();
        std::cerr << "id = " << id << " waiting to acquire the lock\n";
        auto scoped_lock = co_await m.lock();

        // Should always be locked upon acquiring the locks.
        REQUIRE_FALSE(m.try_lock());

        std::cerr << "id = " << id << " lock acquired\n";
        value.fetch_add(1, std::memory_order::relaxed);
        std::cerr << "id = " << id << " coroutine done\n";
        co_return;
    };

    auto make_block_task = [](coro::thread_pool& tp, coro::mutex& m, coro::event& e) -> coro::task<void>
    {
        co_await tp.schedule();
        std::cerr << "block task acquiring lock\n";
        auto scoped_lock = co_await m.lock();
        REQUIRE_FALSE(m.try_lock());
        std::cerr << "block task acquired lock, waiting on event\n";
        co_await e;
        co_return;
    };

    auto make_set_task = [](coro::thread_pool& tp, coro::event& e) -> coro::task<void>
    {
        co_await tp.schedule();
        std::cerr << "set task setting event\n";
        e.set();
        co_return;
    };

    // Grab mutex so all threads block.
    tasks.emplace_back(make_block_task(tp, m, e));

    // Create N tasks that attempt to lock the mutex.
    for (uint64_t i = 1; i <= 4; ++i)
    {
        tasks.emplace_back(make_task(tp, m, value, i));
    }

    tasks.emplace_back(make_set_task(tp, e));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(value == 4);
}

TEST_CASE("mutex scoped_lock unlock prior to scope exit", "[mutex]")
{
    coro::mutex m;

    auto make_task = [](coro::mutex& m) -> coro::task<void>
    {
        {
            auto lk = co_await m.lock();
            REQUIRE_FALSE(m.try_lock());
            lk.unlock();
            REQUIRE(m.try_lock());
        }
        co_return;
    };

    coro::sync_wait(make_task(m));
}

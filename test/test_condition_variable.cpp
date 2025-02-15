#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <queue>

TEST_CASE("condition_variable single waiter", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    auto                     sched = coro::io_scheduler::make_shared();
    coro::mutex              m;
    coro::condition_variable cv(sched);

    auto make_test_task =
        [](coro::io_scheduler* sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<void>
    {
        co_await sched->schedule();

        {
            auto ulock = co_await m.lock();

            REQUIRE(co_await cv.wait_for(ulock, 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE_FALSE(co_await cv.wait_for(ulock, 8ms, []() { return false; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_for(ulock, 8ms, []() { return true; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(ulock, system_clock::now() + 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(ulock, steady_clock::now() + 8ms) == std::cv_status::timeout);
            REQUIRE_FALSE(m.try_lock());

            REQUIRE_FALSE(co_await cv.wait_until(ulock, steady_clock::now() + 8ms, []() { return false; }));
            REQUIRE_FALSE(m.try_lock());

            REQUIRE(co_await cv.wait_until(ulock, steady_clock::now() + 8ms, []() { return true; }));
            REQUIRE_FALSE(m.try_lock());
        }

        auto make_locked_task_1 =
            [](coro::io_scheduler* sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.lock();

            co_await cv.wait(ulock);

            // unreachable
            co_return true;
        };

        REQUIRE_FALSE((co_await sched->schedule(make_locked_task_1(sched, m, cv), 8ms)).has_value());

        auto make_locked_task_2 =
            [](coro::io_scheduler* sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.lock();

            co_await cv.wait(ulock, []() { return false; });

            // unreachable
            co_return true;
        };

        REQUIRE_FALSE((co_await sched->schedule(make_locked_task_2(sched, m, cv), 8ms)).has_value());

        auto make_unlocked_task =
            [](coro::io_scheduler* sched, coro::mutex& m, coro::condition_variable& cv) -> coro::task<bool>
        {
            co_await sched->schedule();
            auto ulock = co_await m.lock();

            co_await cv.wait(ulock, []() { return true; });
            co_return true;
        };

        REQUIRE((co_await sched->schedule(make_unlocked_task(sched, m, cv), 8ms)).has_value());
        co_return;
    };

    coro::sync_wait(make_test_task(sched.get(), m, cv));
    std::this_thread::sleep_for(32ms);
    sched->shutdown();
}

TEST_CASE("condition_variable one notifier and one waiter", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    struct BaseParams
    {
        std::shared_ptr<coro::io_scheduler> sched = coro::io_scheduler::make_shared();
        coro::mutex                         m;
        coro::condition_variable            cv{sched};
    };

    BaseParams bp;

    auto make_notifier_task = [](BaseParams& bp, milliseconds start_delay = 0ms, bool all = false) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        co_await bp.sched->schedule_after(start_delay);
        if (all)
            bp.cv.notify_all();
        else
            bp.cv.notify_one();
        co_return;
    };

    auto make_waiter_task =
        [](BaseParams& bp, milliseconds start_delay = 0ms, milliseconds timeout = 16ms) -> coro::task<bool>
    {
        co_await bp.sched->schedule();
        co_await bp.sched->schedule_after(start_delay);
        auto ulock  = co_await bp.m.lock();
        bool result = co_await bp.cv.wait_for(ulock, timeout) == std::cv_status::no_timeout;
        co_return result;
    };

    REQUIRE_FALSE(std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp), make_waiter_task(bp, 16ms))))
                      .return_value());
    REQUIRE_FALSE(
        std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 0ms, true), make_waiter_task(bp, 16ms))))
            .return_value());
    REQUIRE(
        std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 8ms), make_waiter_task(bp)))).return_value());
    REQUIRE(std::get<1>(coro::sync_wait(coro::when_all(make_notifier_task(bp, 8ms, true), make_waiter_task(bp))))
                .return_value());

    std::this_thread::sleep_for(32ms);
    bp.sched->shutdown();
}

TEST_CASE("condition_variable notify_all", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    struct BaseParams
    {
        std::shared_ptr<coro::io_scheduler> sched = coro::io_scheduler::make_shared();
        coro::mutex                         m;
        coro::condition_variable            cv{sched};
        int                                 number_of_timeouts{};
    };

    BaseParams bp;

    auto make_notifier_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        co_await bp.sched->schedule_after(32ms);
        bp.cv.notify_all();
        co_return;
    };

    auto make_waiter_task = [](BaseParams& bp, milliseconds timeout = 100ms) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        auto ulock = co_await bp.m.lock();
        if (co_await bp.cv.wait_for(ulock, timeout) == std::cv_status::timeout)
            ++bp.number_of_timeouts;
        co_return;
    };

    const int                     num_tasks{32};
    std::vector<coro::task<void>> tasks{};

    for (int64_t i = 0; i < num_tasks; ++i)
    {
        tasks.emplace_back(make_waiter_task(bp));
    }

    tasks.emplace_back(make_notifier_task(bp));

    coro::sync_wait(coro::when_all(std::move(tasks)));
    std::this_thread::sleep_for(256ms);
    bp.sched->shutdown();

    REQUIRE(bp.number_of_timeouts == 0);
}

TEST_CASE("condition_variable for thread-safe-queue between producers and consumers", "[condition_variable]")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    struct BaseParams
    {
        std::shared_ptr<coro::io_scheduler> sched = coro::io_scheduler::make_shared();
        coro::mutex                         m;
        coro::condition_variable            cv{sched};
        std::atomic_bool                    cancel{false};
        std::atomic_int32_t                 next{0};
        int32_t                             max_value{10000};
        std::queue<int32_t>                 q;
        std::set<int32_t>                   values_not_delivered;
        std::set<int32_t>                   values_not_produced;
    };

    BaseParams bp;

    auto make_producer_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        co_await bp.sched->schedule_after(32ms);
        while (!bp.cancel.load(std::memory_order::acquire))
        {
            {
                auto ulock = co_await bp.m.lock();
                auto value = bp.next.fetch_add(1);

                // limit for end of test
                if (value >= bp.max_value)
                    break;

                bp.values_not_delivered.insert(value);
                bp.q.push(value);
            }
            bp.cv.notify_one();
        }
        co_return;
    };

    auto make_consumer_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        while (true)
        {
            auto ulock = co_await bp.m.lock();
            co_await bp.cv.wait(ulock, [&bp]() { return bp.q.size() | bp.cancel.load(std::memory_order::acquire); });
            if (bp.cancel.load(std::memory_order::acquire))
                break;

            auto value = bp.q.front();
            bp.q.pop();
            auto ok = bp.values_not_delivered.erase(value);
            if (!ok)
                bp.values_not_produced.insert(value);
        }

        co_return;
    };

    auto make_director_task = [](BaseParams& bp) -> coro::task<void>
    {
        co_await bp.sched->schedule();
        co_await bp.sched->schedule_after(32ms);
        while (true)
        {
            {
                auto ulock = co_await bp.m.lock();
                if ((bp.next.load(std::memory_order::acquire) >= bp.max_value) && bp.q.empty())
                    break;
            }
            co_await bp.sched->schedule_after(256ms);
        }

        bp.cancel.store(true, std::memory_order::release);
        bp.cv.notify_all();
        co_return;
    };

    std::vector<coro::task<void>> tasks{};

    for (int64_t i = 0; i < 64; ++i)
    {
        tasks.emplace_back(make_consumer_task(bp));
    }

    for (int64_t i = 0; i < 16; ++i)
    {
        tasks.emplace_back(make_producer_task(bp));
    }

    tasks.emplace_back(make_director_task(bp));

    coro::sync_wait(coro::when_all(std::move(tasks)));
    std::this_thread::sleep_for(256ms);
    bp.sched->shutdown();

    REQUIRE(bp.values_not_delivered.size() == 0);
    REQUIRE(bp.values_not_produced.size() == 0);
}

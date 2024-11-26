#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>
#include <iostream>
#include <stop_token>

TEST_CASE("when_any two tasks", "[when_any]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    std::vector<coro::task<uint64_t>> tasks{};
    tasks.emplace_back(make_task(1));
    tasks.emplace_back(make_task(2));

    auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
    REQUIRE(result == 1);
}

#ifdef LIBCORO_FEATURE_NETWORKING

TEST_CASE("when_any two tasks one long running", "[when_any]")
{
    auto s = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_task = [](std::shared_ptr<coro::io_scheduler> s, uint64_t amount) -> coro::task<uint64_t>
    {
        co_await s->schedule();
        if (amount == 1)
        {
            co_await s->yield_for(std::chrono::milliseconds{100});
        }
        co_return amount;
    };

    std::vector<coro::task<uint64_t>> tasks{};
    tasks.emplace_back(make_task(s, 1));
    tasks.emplace_back(make_task(s, 2));

    auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
    REQUIRE(result == 2);

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
}

TEST_CASE("when_any two tasks one long running with cancellation", "[when_any]")
{
    std::stop_source stop_source{};
    auto             s = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_task =
        [](std::shared_ptr<coro::io_scheduler> s, std::stop_token stop_token, uint64_t amount) -> coro::task<uint64_t>
    {
        co_await s->schedule();
        try
        {
            if (amount == 1)
            {
                std::cerr << "yielding with amount=" << amount << "\n";
                co_await s->yield_for(std::chrono::milliseconds{100});
                if (stop_token.stop_requested())
                {
                    std::cerr << "throwing\n";
                    throw std::runtime_error{"task was cancelled"};
                }
                else
                {
                    std::cerr << "not throwing\n";
                }
            }
        }
        catch (const std::exception& e)
        {
            REQUIRE(amount == 1);
            REQUIRE(e.what() == std::string{"task was cancelled"});
        }
        co_return amount;
    };

    std::vector<coro::task<uint64_t>> tasks{};
    tasks.emplace_back(make_task(s, stop_source.get_token(), 1));
    tasks.emplace_back(make_task(s, stop_source.get_token(), 2));

    auto result = coro::sync_wait(coro::when_any(std::move(stop_source), std::move(tasks)));
    REQUIRE(result == 2);

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
}

TEST_CASE("when_any timeout", "[when_any]")
{
    auto scheduler = coro::io_scheduler::make_shared();

    auto make_long_running_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                                     std::chrono::milliseconds           execution_time) -> coro::task<int64_t>
    {
        co_await scheduler->schedule();
        co_await scheduler->yield_for(execution_time);
        co_return 1;
    };

    auto make_timeout_task = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<int64_t>
    {
        co_await scheduler->schedule_after(std::chrono::milliseconds{100});
        co_return -1;
    };

    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{50}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        REQUIRE(result == 1);
    }

    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{500}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        REQUIRE(result == -1);
    }
}

#endif

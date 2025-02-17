#include "catch_amalgamated.hpp"

#include <chrono>
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
    auto scheduler = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 2}});

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

TEST_CASE("when_any io_scheduler::schedule(task, timeout)", "[when_any]")
{
    auto scheduler = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 2}});

    auto make_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                        std::chrono::milliseconds           execution_time) -> coro::task<int64_t>
    {
        co_await scheduler->yield_for(execution_time);
        co_return 1;
    };

    {
        auto result = coro::sync_wait(
            scheduler->schedule(make_task(scheduler, std::chrono::milliseconds{10}), std::chrono::milliseconds{50}));
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 1);
    }

    {
        auto result = coro::sync_wait(
            scheduler->schedule(make_task(scheduler, std::chrono::milliseconds{50}), std::chrono::milliseconds{10}));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == coro::timeout_status::timeout);
    }
}

    #ifndef EMSCRIPTEN
TEST_CASE("when_any io_scheduler::schedule(task, timeout stop_token)", "[when_any]")
{
    auto scheduler = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 2}});

    auto make_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                        std::chrono::milliseconds           execution_time,
                        std::stop_token                     stop_token) -> coro::task<int64_t>
    {
        co_await scheduler->yield_for(execution_time);
        if (stop_token.stop_requested())
        {
            co_return -1;
        }
        co_return 1;
    };

    {
        std::stop_source stop_source{};
        auto             result = coro::sync_wait(scheduler->schedule(
            std::move(stop_source),
            make_task(scheduler, std::chrono::milliseconds{10}, stop_source.get_token()),
            std::chrono::milliseconds{50}));
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 1);
    }

    {
        std::stop_source stop_source{};
        auto             result = coro::sync_wait(scheduler->schedule(
            std::move(stop_source),
            make_task(scheduler, std::chrono::milliseconds{50}, stop_source.get_token()),
            std::chrono::milliseconds{10}));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == coro::timeout_status::timeout);
    }
}
    #endif

TEST_CASE("when_any tuple multiple", "[when_any]")
{
    using namespace std::chrono_literals;

    auto scheduler = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 4}});

    auto make_task1 = [](std::shared_ptr<coro::io_scheduler> scheduler,
                         std::chrono::milliseconds           execution_time) -> coro::task<int>
    {
        co_await scheduler->schedule_after(execution_time);
        co_return 1;
    };

    auto make_task2 = [](std::shared_ptr<coro::io_scheduler> scheduler,
                         std::chrono::milliseconds           execution_time) -> coro::task<double>
    {
        co_await scheduler->schedule_after(execution_time);
        co_return 3.14;
    };

    auto make_task3 = [](std::shared_ptr<coro::io_scheduler> scheduler,
                         std::chrono::milliseconds           execution_time) -> coro::task<std::string>
    {
        co_await scheduler->schedule_after(execution_time);
        co_return std::string{"hello world"};
    };

    {
        auto result = coro::sync_wait(
            coro::when_any(make_task1(scheduler, 10ms), make_task2(scheduler, 50ms), make_task3(scheduler, 50ms)));
        REQUIRE(result.index() == 0);
        REQUIRE(std::get<0>(result) == 1);
    }

    {
        auto result = coro::sync_wait(
            coro::when_any(make_task1(scheduler, 50ms), make_task2(scheduler, 10ms), make_task3(scheduler, 50ms)));
        REQUIRE(result.index() == 1);
        REQUIRE(std::get<1>(result) == 3.14);
    }

    {
        auto result = coro::sync_wait(
            coro::when_any(make_task1(scheduler, 50ms), make_task2(scheduler, 50ms), make_task3(scheduler, 10ms)));
        REQUIRE(result.index() == 2);
        REQUIRE(std::get<2>(result) == "hello world");
    }
}

#endif

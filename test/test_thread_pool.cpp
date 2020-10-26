#include "catch.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("thread_pool one worker, one task")
{
    coro::thread_pool tp{1};

    auto func = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule().value(); // Schedule this coroutine on the scheduler.
        co_return 42;
    };

    auto result = coro::sync_wait(func());
    REQUIRE(result == 42);
}

TEST_CASE("thread_pool one worker, many tasks tuple")
{
    coro::thread_pool tp{1};

    auto f = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule().value(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    auto tasks = coro::sync_wait(coro::when_all_awaitable(f(), f(), f(), f(), f()));
    REQUIRE(std::tuple_size<decltype(tasks)>() == 5);

    uint64_t counter{0};
    std::apply([&counter](auto&&... t) -> void {
            ((counter += t.return_value()), ...);
        },
        tasks);

    REQUIRE(counter == 250);
}

TEST_CASE("thread_pool one worker, many tasks vector")
{
    coro::thread_pool tp{1};

    auto f = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule().value(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    std::vector<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(f());
    input_tasks.emplace_back(f());
    input_tasks.emplace_back(f());

    auto output_tasks = coro::sync_wait(coro::when_all_awaitable(input_tasks));

    REQUIRE(output_tasks.size() == 3);

    uint64_t counter{0};
    for(const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == 150);
}

TEST_CASE("thread_pool N workers, 1 million tasks")
{
    constexpr const std::size_t iterations = 100'000;
    coro::thread_pool tp{};

    auto make_task = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        co_await tp.schedule().value();
        co_return 1;
    };

    std::vector<coro::task<uint64_t>> input_tasks{};
    input_tasks.reserve(iterations);
    for(std::size_t i = 0; i < iterations; ++i)
    {
        input_tasks.emplace_back(make_task(tp));
    }

    auto output_tasks = coro::sync_wait(coro::when_all_awaitable(input_tasks));
    REQUIRE(output_tasks.size() == iterations);

    uint64_t counter{0};
    for(const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == iterations);
}

TEST_CASE("thread pool 1 worker, task spawns another task")
{
    coro::thread_pool tp{};

    auto f1 = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        co_await tp.schedule().value();

        auto f2 = [](coro::thread_pool& tp) -> coro::task<uint64_t>
        {
            co_await tp.schedule().value();
            co_return 5;
        };

        co_return 1 + co_await f2(tp);
    };

    REQUIRE(coro::sync_wait(f1(tp)) == 6);
}

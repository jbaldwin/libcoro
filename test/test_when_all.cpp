#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <list>
#include <ranges>
#include <vector>

TEST_CASE("when_all single task with tuple container", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    auto output_tasks = coro::sync_wait(coro::when_all(make_task(100)));
    REQUIRE(std::tuple_size<decltype(output_tasks)>() == 1);

    uint64_t counter{0};
    std::apply([&counter](auto&&... tasks) -> void { ((counter += tasks.return_value()), ...); }, output_tasks);

    REQUIRE(counter == 100);
}

TEST_CASE("when_all single task with tuple container by move", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    auto t            = make_task(100);
    auto output_tasks = coro::sync_wait(coro::when_all(std::move(t)));
    REQUIRE(std::tuple_size<decltype(output_tasks)>() == 1);

    uint64_t counter{0};
    std::apply([&counter](auto&&... tasks) -> void { ((counter += tasks.return_value()), ...); }, output_tasks);

    REQUIRE(counter == 100);
}

TEST_CASE("when_all multiple tasks with tuple container", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    auto output_tasks = coro::sync_wait(coro::when_all(make_task(100), make_task(50), make_task(20)));
    REQUIRE(std::tuple_size<decltype(output_tasks)>() == 3);

    uint64_t counter{0};
    std::apply([&counter](auto&&... tasks) -> void { ((counter += tasks.return_value()), ...); }, output_tasks);

    REQUIRE(counter == 170);
}

TEST_CASE("when_all single task with vector container", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    std::vector<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(make_task(100));

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(input_tasks)));
    REQUIRE(output_tasks.size() == 1);

    uint64_t counter{0};
    for (const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == 100);
}

TEST_CASE("when_all multple task withs vector container", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    std::vector<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(make_task(100));
    input_tasks.emplace_back(make_task(200));
    input_tasks.emplace_back(make_task(550));
    input_tasks.emplace_back(make_task(1000));

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(input_tasks)));
    REQUIRE(output_tasks.size() == 4);

    uint64_t counter{0};
    for (const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == 1850);
}

TEST_CASE("when_all multple task withs list container", "[when_all]")
{
    auto make_task = [](uint64_t amount) -> coro::task<uint64_t> { co_return amount; };

    std::list<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(make_task(100));
    input_tasks.emplace_back(make_task(200));
    input_tasks.emplace_back(make_task(550));
    input_tasks.emplace_back(make_task(1000));

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(input_tasks)));
    REQUIRE(output_tasks.size() == 4);

    uint64_t counter{0};
    for (const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == 1850);
}

TEST_CASE("when_all inside coroutine", "[when_all]")
{
    coro::thread_pool tp{};
    auto              make_task = [](coro::thread_pool& tp, uint64_t amount) -> coro::task<uint64_t>
    {
        co_await tp.schedule();
        co_return amount;
    };

    auto runner_task = [](coro::thread_pool& tp, auto make_task) -> coro::task<uint64_t>
    {
        std::list<coro::task<uint64_t>> tasks;
        tasks.emplace_back(make_task(tp, 1));
        tasks.emplace_back(make_task(tp, 2));
        tasks.emplace_back(make_task(tp, 3));

        auto output_tasks = co_await coro::when_all(std::move(tasks));

        uint64_t result{0};
        for (const auto& task : output_tasks)
        {
            result += task.return_value();
        }
        co_return result;
    };

    auto result = coro::sync_wait(runner_task(tp, make_task));

    REQUIRE(result == (1 + 2 + 3));
}

TEST_CASE("when_all use std::ranges::view", "[when_all]")
{
    coro::thread_pool tp{};

    auto make_runner_task = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        auto make_task = [](coro::thread_pool& tp, uint64_t amount) -> coro::task<uint64_t>
        {
            co_await tp.schedule();
            co_return amount;
        };
        std::vector<coro::task<uint64_t>> tasks;
        tasks.emplace_back(make_task(tp, 1));
        tasks.emplace_back(make_task(tp, 2));
        tasks.emplace_back(make_task(tp, 3));

        auto output_tasks = co_await coro::when_all(std::ranges::views::all(tasks));

        uint64_t result{0};
        for (const auto& task : output_tasks)
        {
            result += task.return_value();
        }
        co_return result;
    };

    auto result = coro::sync_wait(make_runner_task(tp));
    REQUIRE(result == (1 + 2 + 3));
}

TEST_CASE("when_all each task throws", "[when_all]")
{
    coro::thread_pool tp{};

    auto make_task = [](coro::thread_pool& tp, uint64_t i) -> coro::task<uint64_t>
    {
        co_await tp.schedule();
        if (i % 2 == 0)
        {
            throw std::runtime_error{std::to_string(i)};
        }
        co_return i;
    };

    std::vector<coro::task<uint64_t>> tasks;
    for (auto i = 1; i <= 4; ++i)
    {
        tasks.emplace_back(make_task(tp, i));
    }

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(tasks)));
    for (auto i = 1; i <= 4; ++i)
    {
        auto& task = output_tasks.at(i - 1);
        if (i % 2 == 0)
        {
            REQUIRE_THROWS(task.return_value());
        }
        else
        {
            REQUIRE((int)task.return_value() == i);
        }
    }
}

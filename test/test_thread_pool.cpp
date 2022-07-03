#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("thread_pool one worker one task", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto func = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule(); // Schedule this coroutine on the scheduler.
        co_return 42;
    };

    auto result = coro::sync_wait(func());
    REQUIRE(result == 42);
}

TEST_CASE("thread_pool one worker many tasks tuple", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto f = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    auto tasks = coro::sync_wait(coro::when_all(f(), f(), f(), f(), f()));
    REQUIRE(std::tuple_size<decltype(tasks)>() == 5);

    uint64_t counter{0};
    std::apply([&counter](auto&&... t) -> void { ((counter += t.return_value()), ...); }, tasks);

    REQUIRE(counter == 250);
}

TEST_CASE("thread_pool one worker many tasks vector", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto f = [&tp]() -> coro::task<uint64_t>
    {
        co_await tp.schedule(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    std::vector<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(f());
    input_tasks.emplace_back(f());
    input_tasks.emplace_back(f());

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(input_tasks)));

    REQUIRE(output_tasks.size() == 3);

    uint64_t counter{0};
    for (const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == 150);
}

TEST_CASE("thread_pool N workers 100k tasks", "[thread_pool]")
{
    constexpr const std::size_t iterations = 100'000;
    coro::thread_pool           tp{};

    auto make_task = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        co_await tp.schedule();
        co_return 1;
    };

    std::vector<coro::task<uint64_t>> input_tasks{};
    input_tasks.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i)
    {
        input_tasks.emplace_back(make_task(tp));
    }

    auto output_tasks = coro::sync_wait(coro::when_all(std::move(input_tasks)));
    REQUIRE(output_tasks.size() == iterations);

    uint64_t counter{0};
    for (const auto& task : output_tasks)
    {
        counter += task.return_value();
    }

    REQUIRE(counter == iterations);
}

TEST_CASE("thread_pool 1 worker task spawns another task", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto f1 = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        co_await tp.schedule();

        auto f2 = [](coro::thread_pool& tp) -> coro::task<uint64_t>
        {
            co_await tp.schedule();
            co_return 5;
        };

        co_return 1 + co_await f2(tp);
    };

    REQUIRE(coro::sync_wait(f1(tp)) == 6);
}

TEST_CASE("thread_pool shutdown", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto f = [](coro::thread_pool& tp) -> coro::task<bool>
    {
        try
        {
            co_await tp.schedule();
        }
        catch (...)
        {
            co_return true;
        }
        co_return false;
    };

    tp.shutdown();

    REQUIRE(coro::sync_wait(f(tp)) == true);
}

TEST_CASE("thread_pool schedule functor", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    auto f = []() -> uint64_t { return 1; };

    auto result = coro::sync_wait(tp.schedule(f));
    REQUIRE(result == 1);

    tp.shutdown();

    REQUIRE_THROWS(coro::sync_wait(tp.schedule(f)));
}

TEST_CASE("thread_pool schedule functor return_type = void", "[thread_pool]")
{
    coro::thread_pool tp{coro::thread_pool::options{1}};

    std::atomic<uint64_t> counter{0};
    auto                  f = [](std::atomic<uint64_t>& c) -> void { c++; };

    coro::sync_wait(tp.schedule(f, std::ref(counter)));
    REQUIRE(counter == 1);

    tp.shutdown();

    REQUIRE_THROWS(coro::sync_wait(tp.schedule(f, std::ref(counter))));
}

TEST_CASE("thread_pool event jump threads", "[thread_pool]")
{
    // This test verifies that the thread that sets the event ends up executing every waiter on the event

    coro::thread_pool tp1{coro::thread_pool::options{.thread_count = 1}};
    coro::thread_pool tp2{coro::thread_pool::options{.thread_count = 1}};

    coro::event e{};

    auto make_tp1_task = [&]() -> coro::task<void>
    {
        co_await tp1.schedule();
        auto before_thread_id = std::this_thread::get_id();
        std::cerr << "before event thread_id = " << before_thread_id << "\n";
        co_await e;
        auto after_thread_id = std::this_thread::get_id();
        std::cerr << "after event thread_id = " << after_thread_id << "\n";

        REQUIRE(before_thread_id != after_thread_id);

        co_return;
    };

    auto make_tp2_task = [&]() -> coro::task<void>
    {
        co_await tp2.schedule();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        std::cerr << "setting event\n";
        e.set();
        co_return;
    };

    coro::sync_wait(coro::when_all(make_tp1_task(), make_tp2_task()));
}
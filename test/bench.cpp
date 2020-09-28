#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <atomic>
#include <iomanip>

using namespace std::chrono_literals;
using sc = std::chrono::steady_clock;

static auto print_stats(
    const std::string& bench_name,
    uint64_t operations,
    sc::time_point start,
    sc::time_point stop
) -> void
{
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    std::cout << bench_name << "\n";
    std::cout << "    " << operations << " ops in " << ms.count() << "ms\n";

    double seconds = duration.count() / 1'000'000'000.0;
    double ops_per_sec = static_cast<uint64_t>(operations / seconds);

    std::cout << "    ops/sec: " << std::fixed << ops_per_sec << "\n";
}

TEST_CASE("benchmark counter task")
{
    constexpr std::size_t iterations = 1'000'000;

    {
        coro::scheduler s1{};
        std::atomic<uint64_t> counter{0};
        auto func = [&]() -> coro::task<void>
        {
            ++counter;
            co_return;
        };

        auto start = sc::now();

        for(std::size_t i = 0; i < iterations; ++i)
        {
            s1.schedule(func());
        }

        s1.shutdown();
        print_stats("benchmark counter task through scheduler", iterations, start, sc::now());
        REQUIRE(s1.empty());
        REQUIRE(counter == iterations);
    }

    {
        std::atomic<uint64_t> counter{0};
        auto func = [&]() -> coro::task<void>
        {
            ++counter;
            co_return;
        };

        auto start = sc::now();

        for(std::size_t i = 0; i < iterations; ++i)
        {
            coro::sync_wait(func);
        }

        print_stats("benchmark counter func coro::sync_wait(awaitable)", iterations, start, sc::now());
        REQUIRE(counter == iterations);
    }

    {
        std::atomic<uint64_t> counter{0};
        auto func = [&]() -> coro::task<void>
        {
            ++counter;
            co_return;
        };

        auto start = sc::now();

        for(std::size_t i = 0; i < iterations; ++i)
        {
            coro::sync_wait_task<void>(func());
        }

        print_stats("benchmark counter func coro::sync_wait(task)", iterations, start, sc::now());
        REQUIRE(counter == iterations);
    }

    {
        std::atomic<uint64_t> counter{0};
        auto func = [&]() -> void
        {
            ++counter;
            return;
        };

        auto start = sc::now();

        for(std::size_t i = 0; i < iterations; ++i)
        {
            func();
        }

        print_stats("benchmark counter func direct call", iterations, start, sc::now());
        REQUIRE(counter == iterations);
    }
}

TEST_CASE("benchmark task with yield resume token from main thread")
{
    constexpr std::size_t iterations = 1'000'000;

    coro::scheduler s{};
    coro::resume_token<void> rt{s};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s);
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.yield<void>(tokens[index]);
        ++counter;
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i));
    }

    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens[i].resume();
    }

    s.shutdown();

    auto stop = sc::now();
    print_stats("benchmark task with yield resume token from main thread", iterations, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark task with yield resume token from separate coroutine (resume second)")
{
    constexpr std::size_t iterations = 50'000;

    coro::scheduler s{};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s);
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.yield<void>(tokens[index]);
        ++counter;
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void>
    {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i));
        s.schedule(resume_func(i));
    }

    s.shutdown();

    auto stop = sc::now();
    print_stats("benchmark task with yield resume token from separate coroutine (resume second)", iterations, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark task with yield resume token from separate coroutine (resume first)")
{
    constexpr std::size_t iterations = 1'000'000;

    coro::scheduler s{};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s);
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.yield<void>(tokens[index]);
        ++counter;
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void>
    {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(resume_func(i));
        s.schedule(wait_func(i));
    }

    s.shutdown();

    auto stop = sc::now();
    print_stats("benchmark task with yield resume token from separate coroutine (resume first)", iterations, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark task with yield resume token from separate coroutine (alloc all upfront)")
{
    constexpr std::size_t iterations = 50'000;

    coro::scheduler s{iterations};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s);
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.yield<void>(tokens[index]);
        ++counter;
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void>
    {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i));
    }

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(resume_func(i));
    }

    s.shutdown();

    auto stop = sc::now();
    print_stats("benchmark task with yield resume token from separate coroutine (alloc all upfront)", iterations, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

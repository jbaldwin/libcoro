#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <atomic>
#include <iomanip>

using namespace std::chrono_literals;
using sc = std::chrono::steady_clock;

constexpr std::size_t default_iterations = 5'000'000;

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

TEST_CASE("benchmark counter func direct call")
{
    constexpr std::size_t iterations = default_iterations;
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

TEST_CASE("benchmark counter func coro::sync_wait(awaitable)")
{
    constexpr std::size_t iterations = default_iterations;
    std::atomic<uint64_t> counter{0};
    auto func = [&]() -> coro::task<void>
    {
        ++counter;
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; ++i)
    {

        coro::sync_wait(func());
    }

    print_stats("benchmark counter func coro::sync_wait(awaitable)", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter func coro::sync_wait_all(awaitable)")
{
    constexpr std::size_t iterations = default_iterations;
    std::atomic<uint64_t> counter{0};
    auto func = [&]() -> coro::task<void>
    {
        ++counter;
        co_return;
    };

    auto start = sc::now();

    for(std::size_t i = 0; i < iterations; i += 10)
    {
        coro::sync_wait_all(func(), func(), func(), func(), func(), func(), func(), func(), func(), func());
    }

    print_stats("benchmark counter func coro::sync_wait_all(awaitable)", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler")
{
    constexpr std::size_t iterations = default_iterations;

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

TEST_CASE("benchmark counter task scheduler yield -> resume from main")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops = iterations * 2; // the external resume is still a resume op

    coro::scheduler s{};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
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
    print_stats("benchmark counter task scheduler yield -> resume from main", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler yield -> resume from coroutine")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler s{};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
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
    print_stats("benchmark counter task scheduler yield -> resume from coroutine", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler resume from coroutine -> yield")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler s{};
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
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
    print_stats("benchmark counter task scheduler resume from coroutine -> yield", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler yield (all) -> resume (all) from coroutine with reserve")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler s{ coro::scheduler::options { .reserve_size = iterations } };
    std::vector<coro::resume_token<void>> tokens{};
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
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
    print_stats("benchmark counter task scheduler yield -> resume from coroutine with reserve", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

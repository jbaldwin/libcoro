#include "catch.hpp"

#include <coro/coro.hpp>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace std::chrono_literals;
using sc = std::chrono::steady_clock;

constexpr std::size_t default_iterations = 5'000'000;

static auto print_stats(const std::string& bench_name, uint64_t operations, sc::time_point start, sc::time_point stop)
    -> void
{
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    auto ms       = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    std::cout << bench_name << "\n";
    std::cout << "    " << operations << " ops in " << ms.count() << "ms\n";

    double seconds     = duration.count() / 1'000'000'000.0;
    double ops_per_sec = static_cast<uint64_t>(operations / seconds);

    std::cout << "    ops/sec: " << std::fixed << ops_per_sec << "\n";
}

TEST_CASE("benchmark counter func direct call")
{
    constexpr std::size_t iterations = default_iterations;
    std::atomic<uint64_t> counter{0};
    auto                  func = [&]() -> void {
        counter.fetch_add(1, std::memory_order::relaxed);
        return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        func();
    }

    print_stats("benchmark counter func direct call", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter func coro::sync_wait(awaitable)")
{
    constexpr std::size_t iterations = default_iterations;
    uint64_t              counter{0};
    auto                  func = []() -> coro::task<uint64_t> { co_return 1; };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        counter += coro::sync_wait(func());
    }

    print_stats("benchmark counter func coro::sync_wait(awaitable)", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter func coro::sync_wait(coro::when_all_awaitable(awaitable)) x10")
{
    constexpr std::size_t iterations = default_iterations;
    uint64_t              counter{0};
    auto                  f = []() -> coro::task<uint64_t> { co_return 1; };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; i += 10)
    {
        auto tasks = coro::sync_wait(coro::when_all_awaitable(f(), f(), f(), f(), f(), f(), f(), f(), f(), f()));

        std::apply([&counter](auto&&... t) { ((counter += t.return_value()), ...); }, tasks);
    }

    print_stats(
        "benchmark counter func coro::sync_wait(coro::when_all_awaitable(awaitable))", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark thread_pool{1} counter task")
{
    constexpr std::size_t iterations = default_iterations;

    coro::thread_pool     tp{coro::thread_pool::options{1}};
    std::atomic<uint64_t> counter{0};

    auto make_task = [](coro::thread_pool& tp, std::atomic<uint64_t>& c) -> coro::task<void> {
        co_await tp.schedule().value();
        c.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    std::vector<coro::task<void>> tasks;
    tasks.reserve(iterations);

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task(tp, counter));
        tasks.back().resume();
    }

    tp.shutdown();

    print_stats("benchmark thread_pool{1} counter task", iterations, start, sc::now());
    REQUIRE(counter == iterations);
    REQUIRE(tp.empty());
}

TEST_CASE("benchmark thread_pool{2} counter task")
{
    constexpr std::size_t iterations = default_iterations;

    coro::thread_pool     tp{coro::thread_pool::options{2}};
    std::atomic<uint64_t> counter{0};

    auto make_task = [](coro::thread_pool& tp, std::atomic<uint64_t>& c) -> coro::task<void> {
        co_await tp.schedule().value();
        c.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    std::vector<coro::task<void>> tasks;
    tasks.reserve(iterations);

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task(tp, counter));
        tasks.back().resume();
    }

    tp.shutdown();

    print_stats("benchmark thread_pool{n} counter task", iterations, start, sc::now());
    REQUIRE(counter == iterations);
    REQUIRE(tp.empty());
}

TEST_CASE("benchmark counter task scheduler")
{
    constexpr std::size_t iterations = default_iterations;

    coro::scheduler       s1{};
    std::atomic<uint64_t> counter{0};
    auto                  func = [&]() -> coro::task<void> {
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
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
    constexpr std::size_t ops        = iterations * 2; // the external resume is still a resume op

    coro::scheduler                       s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void> {
        co_await s.yield<void>(tokens[index]);
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i));
    }

    for (std::size_t i = 0; i < iterations; ++i)
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
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler                       s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void> {
        co_await s.yield<void>(tokens[index]);
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void> {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
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
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler                       s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void> {
        co_await s.yield<void>(tokens[index]);
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void> {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
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
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::scheduler                       s{coro::scheduler::options{.reserve_size = iterations}};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.generate_resume_token<void>());
    }

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void> {
        co_await s.yield<void>(tokens[index]);
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void> {
        tokens[index].resume();
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i));
    }

    for (std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(resume_func(i));
    }

    s.shutdown();

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler yield -> resume from coroutine with reserve", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

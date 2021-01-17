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

TEST_CASE("benchmark counter task io_scheduler")
{
    constexpr std::size_t iterations = default_iterations;

    coro::io_scheduler    s1{};
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
    print_stats("benchmark counter task through io_scheduler", iterations, start, sc::now());
    REQUIRE(s1.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task io_scheduler yield -> resume from main")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // the external resume is still a resume op

    coro::io_scheduler                    s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.make_resume_token<void>());
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
    print_stats("benchmark counter task io_scheduler yield -> resume from main", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task io_scheduler yield -> resume from coroutine")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::io_scheduler                    s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.make_resume_token<void>());
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
    print_stats("benchmark counter task io_scheduler yield -> resume from coroutine", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task io_scheduler resume from coroutine -> yield")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::io_scheduler                    s{};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.make_resume_token<void>());
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
    print_stats("benchmark counter task io_scheduler resume from coroutine -> yield", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task io_scheduler yield (all) -> resume (all) from coroutine with reserve")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // each iteration executes 2 coroutines.

    coro::io_scheduler                    s{coro::io_scheduler::options{.reserve_size = iterations}};
    std::vector<coro::resume_token<void>> tokens{};
    for (std::size_t i = 0; i < iterations; ++i)
    {
        tokens.emplace_back(s.make_resume_token<void>());
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
    print_stats("benchmark counter task io_scheduler yield -> resume from coroutine with reserve", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark tcp_server echo server")
{
    /**
     * This test *requires* two schedulers since polling on read/write of the sockets involved
     * will reset/trample on each other when each side of the client + server go to poll().
     */

    const constexpr std::size_t connections             = 64;
    const constexpr std::size_t messages_per_connection = 10'000;
    const constexpr std::size_t ops                     = connections * messages_per_connection;

    const std::string msg = "im a data point in a stream of bytes";

    coro::io_scheduler server_scheduler{};
    coro::io_scheduler client_scheduler{};

    std::atomic<bool> listening{false};

    auto make_on_connection_task = [&](coro::net::tcp_client client) -> coro::task<void> {
        std::string in(64, '\0');

        // Echo the messages until the socket is closed. a 'done' message arrives.
        while (true)
        {
            auto pstatus = co_await client.poll(coro::poll_op::read);
            REQUIRE(pstatus == coro::poll_status::event);

            auto [rstatus, rspan] = client.recv(in);
            if (rstatus == coro::net::recv_status::closed)
            {
                REQUIRE(rspan.empty());
                break;
            }

            REQUIRE(rstatus == coro::net::recv_status::ok);

            in.resize(rspan.size());

            auto [sstatus, remaining] = client.send(in);
            REQUIRE(sstatus == coro::net::send_status::ok);
            REQUIRE(remaining.empty());
        }

        co_return;
    };

    auto make_server_task = [&]() -> coro::task<void> {
        coro::net::tcp_server server{server_scheduler};

        listening = true;

        uint64_t accepted{0};
        while (accepted < connections)
        {
            auto pstatus = co_await server.poll();
            REQUIRE(pstatus == coro::poll_status::event);

            auto client = server.accept();
            REQUIRE(client.socket().is_valid());

            server_scheduler.schedule(make_on_connection_task(std::move(client)));

            ++accepted;
        }

        co_return;
    };

    auto make_client_task = [&]() -> coro::task<void> {
        coro::net::tcp_client client{client_scheduler};

        auto cstatus = co_await client.connect();
        REQUIRE(cstatus == coro::net::connect_status::connected);

        for (size_t i = 1; i <= messages_per_connection; ++i)
        {
            auto [sstatus, remaining] = client.send(msg);
            REQUIRE(sstatus == coro::net::send_status::ok);
            REQUIRE(remaining.empty());

            auto pstatus = co_await client.poll(coro::poll_op::read);
            REQUIRE(pstatus == coro::poll_status::event);

            std::string response(64, '\0');
            auto [rstatus, rspan] = client.recv(response);
            REQUIRE(rstatus == coro::net::recv_status::ok);
            REQUIRE(rspan.size() == msg.size());
            response.resize(rspan.size());
            REQUIRE(response == msg);
        }

        co_return;
    };

    auto start = sc::now();

    // Create the server to accept incoming tcp connections.
    server_scheduler.schedule(make_server_task());

    // The server can take a small bit of time to start up, if we don't wait for it to notify then
    // the first few connections can easily fail to connect causing this test to fail.
    while (!listening)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Spawn N client connections.
    for (size_t i = 0; i < connections; ++i)
    {
        REQUIRE(client_scheduler.schedule(make_client_task()));
    }

    // Wait for all the connections to complete their work.
    while (!client_scheduler.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    auto stop = sc::now();
    print_stats("benchmark tcp_client and tcp_server", ops, start, stop);

    server_scheduler.shutdown();
    REQUIRE(server_scheduler.empty());

    client_scheduler.shutdown();
    REQUIRE(client_scheduler.empty());
}

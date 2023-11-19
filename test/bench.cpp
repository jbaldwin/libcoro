#include "catch_amalgamated.hpp"

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

TEST_CASE("benchmark counter func direct call", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    std::atomic<uint64_t> counter{0};
    auto                  func = [&]() -> void
    {
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

TEST_CASE("benchmark counter func coro::sync_wait(awaitable)", "[benchmark]")
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

TEST_CASE("benchmark counter func coro::sync_wait(coro::when_all(awaitable)) x10", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    uint64_t              counter{0};
    auto                  f = []() -> coro::task<uint64_t> { co_return 1; };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; i += 10)
    {
        auto tasks = coro::sync_wait(coro::when_all(f(), f(), f(), f(), f(), f(), f(), f(), f(), f()));

        std::apply([&counter](auto&&... t) { ((counter += t.return_value()), ...); }, tasks);
    }

    print_stats("benchmark counter func coro::sync_wait(coro::when_all(awaitable))", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter func coro::sync_wait(coro::when_all(vector<awaitable>)) x10", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    uint64_t              counter{0};
    auto                  f = []() -> coro::task<uint64_t> { co_return 1; };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; i += 10)
    {
        std::vector<coro::task<uint64_t>> tasks{};
        tasks.reserve(10);
        for (size_t j = 0; j < 10; ++j)
        {
            tasks.emplace_back(f());
        }

        auto results = coro::sync_wait(coro::when_all(std::move(tasks)));

        for (const auto& r : results)
        {
            counter += r.return_value();
        }
    }

    print_stats("benchmark counter func coro::sync_wait(coro::when_all(awaitable))", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark thread_pool{1} counter task", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;

    coro::thread_pool     tp{coro::thread_pool::options{1}};
    std::atomic<uint64_t> counter{0};

    auto make_task = [](coro::thread_pool& tp, std::atomic<uint64_t>& c) -> coro::task<void>
    {
        co_await tp.schedule();
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

    // This will fail in valgrind since it runs in a single 'thread', and thus is shutsdown prior
    // to any coroutine actually getting properly scheduled onto the background thread pool.
    // Inject a sleep here so it forces a thread context switch within valgrind.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    tp.shutdown();

    print_stats("benchmark thread_pool{1} counter task", iterations, start, sc::now());
    REQUIRE(counter == iterations);
    REQUIRE(tp.empty());
}

TEST_CASE("benchmark thread_pool{2} counter task", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;

    coro::thread_pool     tp{coro::thread_pool::options{2}};
    std::atomic<uint64_t> counter{0};

    auto make_task = [](coro::thread_pool& tp, std::atomic<uint64_t>& c) -> coro::task<void>
    {
        co_await tp.schedule();
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

    // This will fail in valgrind since it runs in a single 'thread', and thus is shutsdown prior
    // to any coroutine actually getting properly scheduled onto the background thread pool.
    // Inject a sleep here so it forces a thread context switch within valgrind.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    tp.shutdown();

    print_stats("benchmark thread_pool{2} counter task", iterations, start, sc::now());
    REQUIRE(counter == iterations);
    REQUIRE(tp.empty());
}

TEST_CASE("benchmark thread_pool{N} counter task", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;

    coro::thread_pool     tp{};
    std::atomic<uint64_t> counter{0};

    auto make_task = [](coro::thread_pool& tp, std::atomic<uint64_t>& c) -> coro::task<void>
    {
        co_await tp.schedule();
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

    // This will fail in valgrind since it runs in a single 'thread', and thus is shutsdown prior
    // to any coroutine actually getting properly scheduled onto the background thread pool.
    // Inject a sleep here so it forces a thread context switch within valgrind.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    tp.shutdown();

    print_stats("benchmark thread_pool{N} counter task", iterations, start, sc::now());
    REQUIRE(counter == iterations);
    REQUIRE(tp.empty());
}

TEST_CASE("benchmark counter task scheduler{1} yield", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // the external resume is still a resume op

    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    std::atomic<uint64_t>         counter{0};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        co_await s.yield();
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task());
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler{1} yield", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

#ifdef LIBCORO_FEATURE_NETWORKING
TEST_CASE("benchmark counter task scheduler{1} yield_for", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // the external resume is still a resume op

    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    std::atomic<uint64_t>         counter{0};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        co_await s.yield_for(std::chrono::milliseconds{1});
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task());
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler{1} yield", ops, start, stop);
    REQUIRE(s.empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler await event from another coroutine", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 3; // two tasks + event resume

    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    std::vector<std::unique_ptr<coro::event>> events{};
    events.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i)
    {
        events.emplace_back(std::make_unique<coro::event>());
    }

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations * 2); // one for wait, one for resume

    std::atomic<uint64_t> counter{0};

    auto wait_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.schedule();
        co_await *events[index];
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto resume_func = [&](std::size_t index) -> coro::task<void>
    {
        co_await s.schedule();
        events[index]->set();
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(wait_func(i));
        tasks.emplace_back(resume_func(i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler await event from another coroutine", ops, start, stop);
    REQUIRE(counter == iterations);

    // valgrind workaround
    while (!s.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(s.empty());
}

TEST_CASE("benchmark tcp_server echo server thread pool", "[benchmark]")
{
    const constexpr std::size_t connections             = 100;
    const constexpr std::size_t messages_per_connection = 1'000;
    const constexpr std::size_t ops                     = connections * messages_per_connection;

    const std::string msg = "im a data point in a stream of bytes";

    std::atomic<uint64_t> listening{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> clients_completed{0};

    auto make_on_connection_task = [&](coro::net::tcp_client client, coro::latch& wait_for_clients) -> coro::task<void>
    {
        std::string in(64, '\0');

        // Echo the messages until the socket is closed.
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

        wait_for_clients.count_down();
        std::cerr << "wait_for_clients.count_down(1) -> " << wait_for_clients.remaining() << "\n";
        co_return;
    };

    auto server_scheduler = std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_server_task = [&]() -> coro::task<void>
    {
        co_await server_scheduler->schedule();

        coro::latch           wait_for_clients{connections};
        coro::net::tcp_server server{server_scheduler};

        listening++;

        while (accepted.load(std::memory_order::acquire) < connections)
        {
            auto pstatus = co_await server.poll(std::chrono::milliseconds{1});
            if (pstatus == coro::poll_status::event)
            {
                auto c = server.accept();
                if (c.socket().is_valid())
                {
                    accepted.fetch_add(1, std::memory_order::release);
                    server_scheduler->schedule(make_on_connection_task(std::move(c), wait_for_clients));
                }
            }
        }

        std::cerr << "server co_await wait_for_clients\n";
        co_await wait_for_clients;
        co_return;
    };

    std::mutex                                    g_histogram_mutex;
    std::map<std::chrono::milliseconds, uint64_t> g_histogram;

    auto client_scheduler = std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_client_task = [&]() -> coro::task<void>
    {
        co_await client_scheduler->schedule();
        std::map<std::chrono::milliseconds, uint64_t> histogram;
        coro::net::tcp_client                         client{client_scheduler};

        auto cstatus = co_await client.connect(); // std::chrono::seconds{1});
        REQUIRE(cstatus == coro::net::connect_status::connected);

        for (size_t i = 1; i <= messages_per_connection; ++i)
        {
            auto req_start            = std::chrono::steady_clock::now();
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

            auto req_stop = std::chrono::steady_clock::now();
            histogram[std::chrono::duration_cast<std::chrono::milliseconds>(req_stop - req_start)]++;
        }

        {
            std::scoped_lock lk{g_histogram_mutex};
            for (auto [ms, count] : histogram)
            {
                g_histogram[ms] += count;
            }
        }

        clients_completed.fetch_add(1);

        co_return;
    };

    auto start = sc::now();

    // Create the server to accept incoming tcp connections.
    auto server_thread = std::thread{[&]() { coro::sync_wait(make_server_task()); }};

    // The server can take a small bit of time to start up, if we don't wait for it to notify then
    // the first few connections can easily fail to connect causing this test to fail.
    while (listening < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    auto client_thread = std::thread{[&]()
                                     {
                                         std::vector<coro::task<void>> tasks{};
                                         for (size_t i = 0; i < connections; ++i)
                                         {
                                             tasks.emplace_back(make_client_task());
                                         }
                                         coro::sync_wait(coro::when_all(std::move(tasks)));
                                     }};

    std::cerr << "joining client thread...\n";
    client_thread.join();
    std::cerr << "joining server thread...\n";
    server_thread.join();
    std::cerr << "all coroutines joined\n";

    auto stop = sc::now();
    print_stats("benchmark tcp_client and tcp_server thread_pool", ops, start, stop);

    for (const auto& [ms, count] : g_histogram)
    {
        std::cerr << ms.count() << " : " << count << "\n";
    }
}

TEST_CASE("benchmark tcp_server echo server inline", "[benchmark]")
{
    const constexpr std::size_t connections             = 100;
    const constexpr std::size_t messages_per_connection = 1'000;
    const constexpr std::size_t ops                     = connections * messages_per_connection;

    const std::string msg = "im a data point in a stream of bytes";

    const constexpr std::size_t server_count = 10;
    const constexpr std::size_t client_count = 10;

    std::atomic<uint64_t> listening{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> clients_completed{0};

    std::atomic<uint64_t> server_id{0};

    using estrat = coro::io_scheduler::execution_strategy_t;

    struct server
    {
        uint64_t                            id;
        std::shared_ptr<coro::io_scheduler> scheduler{std::make_shared<coro::io_scheduler>(
            coro::io_scheduler::options{.execution_strategy = estrat::process_tasks_inline})};
        uint64_t                            live_clients{0};
        coro::event                         wait_for_clients{};
    };

    struct client
    {
        std::shared_ptr<coro::io_scheduler> scheduler{std::make_shared<coro::io_scheduler>(
            coro::io_scheduler::options{.execution_strategy = estrat::process_tasks_inline})};
        std::vector<coro::task<void>>       tasks{};
    };

    auto make_on_connection_task = [&](server& s, coro::net::tcp_client client) -> coro::task<void>
    {
        std::string in(64, '\0');

        // Echo the messages until the socket is closed.
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

        s.live_clients--;
        if (s.live_clients == 0)
        {
            s.wait_for_clients.set();
        }
        co_return;
    };

    auto make_server_task = [&](server& s) -> coro::task<void>
    {
        co_await s.scheduler->schedule();

        coro::net::tcp_server server{s.scheduler};

        listening++;

        while (accepted.load(std::memory_order::acquire) < connections)
        {
            auto pstatus = co_await server.poll(std::chrono::milliseconds{1});
            if (pstatus == coro::poll_status::event)
            {
                auto c = server.accept();
                if (c.socket().is_valid())
                {
                    accepted.fetch_add(1, std::memory_order::release);

                    s.live_clients++;
                    s.scheduler->schedule(make_on_connection_task(s, std::move(c)));
                }
            }
        }

        co_await s.wait_for_clients;
        co_return;
    };

    std::mutex                                    g_histogram_mutex;
    std::map<std::chrono::milliseconds, uint64_t> g_histogram;

    auto make_client_task = [&](client& c) -> coro::task<void>
    {
        co_await c.scheduler->schedule();
        std::map<std::chrono::milliseconds, uint64_t> histogram;
        coro::net::tcp_client                         client{c.scheduler};

        auto cstatus = co_await client.connect(); // std::chrono::seconds{1});
        REQUIRE(cstatus == coro::net::connect_status::connected);

        for (size_t i = 1; i <= messages_per_connection; ++i)
        {
            auto req_start            = std::chrono::steady_clock::now();
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

            auto req_stop = std::chrono::steady_clock::now();
            histogram[std::chrono::duration_cast<std::chrono::milliseconds>(req_stop - req_start)]++;
        }

        {
            std::scoped_lock lk{g_histogram_mutex};
            for (auto [ms, count] : histogram)
            {
                g_histogram[ms] += count;
            }
        }

        clients_completed.fetch_add(1);

        co_return;
    };

    auto start = sc::now();

    // Create the server to accept incoming tcp connections.
    std::vector<std::thread> server_threads{};
    for (size_t i = 0; i < server_count; ++i)
    {
        server_threads.emplace_back(std::thread{[&]()
                                                {
                                                    server s{};
                                                    s.id = server_id++;
                                                    coro::sync_wait(make_server_task(s));
                                                    s.scheduler->shutdown();
                                                }});
    }

    // The server can take a small bit of time to start up, if we don't wait for it to notify then
    // the first few connections can easily fail to connect causing this test to fail.
    while (listening != server_count)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Spawn N client connections across a set number of clients.
    std::vector<std::thread> client_threads{};
    std::vector<client>      clients{};
    for (size_t i = 0; i < client_count; ++i)
    {
        client_threads.emplace_back(std::thread{[&]()
                                                {
                                                    client c{};
                                                    for (size_t i = 0; i < connections / client_count; ++i)
                                                    {
                                                        c.tasks.emplace_back(make_client_task(c));
                                                    }
                                                    coro::sync_wait(coro::when_all(std::move(c.tasks)));
                                                    c.scheduler->shutdown();
                                                }});
    }

    for (auto& ct : client_threads)
    {
        ct.join();
    }

    for (auto& st : server_threads)
    {
        st.join();
    }

    auto stop = sc::now();
    print_stats("benchmark tcp_client and tcp_server inline", ops, start, stop);

    for (const auto& [ms, count] : g_histogram)
    {
        std::cerr << ms.count() << " : " << count << "\n";
    }
}
#endif

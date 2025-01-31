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
    auto                  func = [](std::atomic<uint64_t>& counter) -> void
    {
        counter.fetch_add(1, std::memory_order::relaxed);
        return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        func(counter);
    }

    print_stats("benchmark counter func direct call", iterations, start, sc::now());
    REQUIRE(counter == iterations);
}

// static auto generate_bytes(uint64_t size) -> std::string
// {
//     using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;

//     random_bytes_engine rbe{};
//     std::string         data{};
//     data.reserve(size);
//     std::generate(begin(data), end(data), std::ref(rbe));
//     return data;
// }

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

    auto s = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::atomic<uint64_t>         counter{0};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);

    auto make_task = [](std::shared_ptr<coro::io_scheduler> s, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await s->schedule();
        co_await s->yield();
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task(s, counter));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler{1} yield", ops, start, stop);
    REQUIRE(s->empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler{1} yield_for", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 2; // the external resume is still a resume op

    auto s = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::atomic<uint64_t>         counter{0};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);

    auto make_task = [](std::shared_ptr<coro::io_scheduler> s, std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        co_await s->schedule();
        co_await s->yield_for(std::chrono::milliseconds{1});
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(make_task(s, counter));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler{1} yield", ops, start, stop);
    REQUIRE(s->empty());
    REQUIRE(counter == iterations);
}

TEST_CASE("benchmark counter task scheduler await event from another coroutine", "[benchmark]")
{
    constexpr std::size_t iterations = default_iterations;
    constexpr std::size_t ops        = iterations * 3; // two tasks + event resume

    auto s = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    std::vector<std::unique_ptr<coro::event>> events{};
    events.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i)
    {
        events.emplace_back(std::make_unique<coro::event>());
    }

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations * 2); // one for wait, one for resume

    std::atomic<uint64_t> counter{0};

    auto wait_func = [](std::shared_ptr<coro::io_scheduler>        s,
                        std::vector<std::unique_ptr<coro::event>>& events,
                        std::atomic<uint64_t>&                     counter,
                        std::size_t                                index) -> coro::task<void>
    {
        co_await s->schedule();
        co_await *events[index];
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto resume_func = [](std::shared_ptr<coro::io_scheduler>        s,
                          std::vector<std::unique_ptr<coro::event>>& events,
                          std::size_t                                index) -> coro::task<void>
    {
        co_await s->schedule();
        events[index]->set();
        co_return;
    };

    auto start = sc::now();

    for (std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(wait_func(s, events, counter, i));
        tasks.emplace_back(resume_func(s, events, i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    auto stop = sc::now();
    print_stats("benchmark counter task scheduler await event from another coroutine", ops, start, stop);
    REQUIRE(counter == iterations);

    // valgrind workaround
    while (!s->empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(s->empty());
}

#ifdef LIBCORO_FEATURE_NETWORKING
TEST_CASE("benchmark tcp::server echo server thread pool", "[benchmark]")
{
    const constexpr std::size_t connections             = 100;
    const constexpr std::size_t messages_per_connection = 1'000;
    const constexpr std::size_t ops                     = connections * messages_per_connection;

    const std::string msg = "im a data point in a stream of bytes";

    std::atomic<uint64_t> listening{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> clients_completed{0};

    auto server_scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_server_task = [](std::shared_ptr<coro::io_scheduler> server_scheduler,
                               std::atomic<uint64_t>&              listening,
                               std::atomic<uint64_t>&              accepted) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tcp::client client,
                                          coro::latch&           wait_for_clients) -> coro::task<void>
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

        co_await server_scheduler->schedule();

        coro::latch            wait_for_clients{connections};
        coro::net::tcp::server server{server_scheduler};

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
                    server_scheduler->spawn(make_on_connection_task(std::move(c), wait_for_clients));
                }
            }
        }

        std::cerr << "server co_await wait_for_clients\n";
        co_await wait_for_clients;
        std::cerr << "server co_return\n";
        co_return;
    };

    std::mutex                                    g_histogram_mutex;
    std::map<std::chrono::milliseconds, uint64_t> g_histogram;

    auto client_scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_client_task = [](std::shared_ptr<coro::io_scheduler>            client_scheduler,
                               const std::string&                             msg,
                               std::atomic<uint64_t>&                         clients_completed,
                               std::map<std::chrono::milliseconds, uint64_t>& g_histogram,
                               std::mutex&                                    g_histogram_mutex) -> coro::task<void>
    {
        co_await client_scheduler->schedule();
        std::map<std::chrono::milliseconds, uint64_t> histogram;
        coro::net::tcp::client                        client{client_scheduler};

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
    auto server_thread =
        std::thread{[&]() { coro::sync_wait(make_server_task(server_scheduler, listening, accepted)); }};

    // The server can take a small bit of time to start up, if we don't wait for it to notify then
    // the first few connections can easily fail to connect causing this test to fail.
    while (listening < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    auto client_thread =
        std::thread{[&]()
                    {
                        std::vector<coro::task<void>> tasks{};
                        for (size_t i = 0; i < connections; ++i)
                        {
                            tasks.emplace_back(make_client_task(
                                client_scheduler, msg, clients_completed, g_histogram, g_histogram_mutex));
                        }
                        coro::sync_wait(coro::when_all(std::move(tasks)));
                    }};

    std::cerr << "joining client thread...\n";
    client_thread.join();
    std::cerr << "joining server thread...\n";
    server_thread.join();
    std::cerr << "all coroutines joined\n";

    auto stop = sc::now();
    print_stats("benchmark tcp::client and tcp::server thread_pool", ops, start, stop);

    for (const auto& [ms, count] : g_histogram)
    {
        std::cerr << ms.count() << " : " << count << "\n";
    }
}

TEST_CASE("benchmark tcp::server echo server inline", "[benchmark]")
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
        std::shared_ptr<coro::io_scheduler> scheduler{coro::io_scheduler::make_shared(
            coro::io_scheduler::options{.execution_strategy = estrat::process_tasks_inline})};
        uint64_t                            live_clients{0};
        coro::event                         wait_for_clients{};
    };

    struct client
    {
        std::shared_ptr<coro::io_scheduler> scheduler{coro::io_scheduler::make_shared(
            coro::io_scheduler::options{.execution_strategy = estrat::process_tasks_inline})};
        std::vector<coro::task<void>>       tasks{};
    };

    auto make_server_task =
        [](server& s, std::atomic<uint64_t>& listening, std::atomic<uint64_t>& accepted) -> coro::task<void>
    {
        auto make_on_connection_task = [](server& s, coro::net::tcp::client client) -> coro::task<void>
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
            std::cerr << "s.live_clients=" << s.live_clients << std::endl;
            if (s.live_clients == 0)
            {
                std::cerr << "s.wait_for_clients.set()" << std::endl;
                s.wait_for_clients.set();
            }
            co_return;
        };

        co_await s.scheduler->schedule();

        coro::net::tcp::server server{s.scheduler};

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
                    s.scheduler->spawn(make_on_connection_task(s, std::move(c)));
                }
            }
        }

        std::cerr << "co_await s.wait_for_clients\n";
        co_await s.wait_for_clients;
        std::cerr << "make_server_task co_return\n";
        co_return;
    };

    std::mutex                                    g_histogram_mutex;
    std::map<std::chrono::milliseconds, uint64_t> g_histogram;

    auto make_client_task = [](client&                                        c,
                               const std::string&                             msg,
                               std::atomic<uint64_t>&                         clients_completed,
                               std::map<std::chrono::milliseconds, uint64_t>& g_histogram,
                               std::mutex&                                    g_histogram_mutex) -> coro::task<void>
    {
        co_await c.scheduler->schedule();
        std::map<std::chrono::milliseconds, uint64_t> histogram;
        coro::net::tcp::client                        client{c.scheduler};

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
                                                    std::cerr << "coro::sync_wait(make_server_task(s));\n";
                                                    coro::sync_wait(make_server_task(s, listening, accepted));
                                                    std::cerr << "server.scheduler->shutdown()\n";
                                                    s.scheduler->shutdown();
                                                    std::cerr << "server thread exiting\n";
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
        client_threads.emplace_back(std::thread{
            [&]()
            {
                client c{};
                for (size_t i = 0; i < connections / client_count; ++i)
                {
                    c.tasks.emplace_back(make_client_task(c, msg, clients_completed, g_histogram, g_histogram_mutex));
                }
                std::cerr << "coro::sync_wait(coro::when_all(std::move(c.tasks)));\n";
                coro::sync_wait(coro::when_all(std::move(c.tasks)));
                std::cerr << "client.scheduler->shutdown()\n";
                c.scheduler->shutdown();
                std::cerr << "client thread exiting\n";
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
    print_stats("benchmark tcp::client and tcp::server inline", ops, start, stop);

    for (const auto& [ms, count] : g_histogram)
    {
        std::cerr << ms.count() << " : " << count << "\n";
    }
}

    #ifdef LIBCORO_FEATURE_TLS
TEST_CASE("benchmark tls::server echo server thread pool", "[benchmark]")
{
    const constexpr std::size_t connections             = 100;
    const constexpr std::size_t messages_per_connection = 1'000;
    const constexpr std::size_t ops                     = connections * messages_per_connection;

    const std::string msg = "im a data point in a stream of bytes";

    std::atomic<uint64_t> listening{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> clients_completed{0};

    auto server_scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_server_task = [](std::shared_ptr<coro::io_scheduler> server_scheduler,
                               std::atomic<uint64_t>&              listening,
                               std::atomic<uint64_t>&              accepted) -> coro::task<void>
    {
        auto make_on_connection_task = [](coro::net::tls::client client,
                                          coro::latch&           wait_for_clients) -> coro::task<void>
        {
            std::string in(64, '\0');

            auto closed = false;
            // Echo the messages until the socket is closed.
            while (!closed)
            {
                auto [rstatus, rspan] = co_await client.recv(in);
                // std::cerr << "SERVER CONNECTION: rstatus =" << coro::net::tls::to_string(rstatus) << "\n";
                std::string_view data{};
                switch (rstatus)
                {
                    case coro::net::tls::recv_status::ok:
                        REQUIRE(rstatus == coro::net::tls::recv_status::ok);
                        data = std::string_view{rspan.begin(), rspan.end()};
                        // std::cerr << "SERVER CONNECTION: recv() -> " << data << "\n";
                        break;
                    case coro::net::tls::recv_status::closed:
                        // std::cerr << "SERVER CONNECTION: closed\n";
                        REQUIRE(rspan.empty());
                        closed = true;
                        break;
                    case coro::net::tls::recv_status::want_read:
                    {
                        // std::cerr << "SERVER CONNECTION: want_read\n";
                    }
                        continue;
                    case coro::net::tls::recv_status::want_write:
                    {
                        // std::cerr << "SERVER CONNECTION: want_write\n";
                    }
                        continue;
                    default:
                        // std::cerr << "SERVER CONNECTION: error (closing)\n";
                        closed = true;
                        break;
                }

                if (closed)
                {
                    break;
                }

                // std::cerr << "SERVER CONNECTION: client.send()\n";
                auto [sstatus, remaining] = co_await client.send(data);
                REQUIRE(sstatus == coro::net::tls::send_status::ok);
                REQUIRE(remaining.empty());
                // std::cerr << "SERVER CONNECTION: send() -> " << data << "\n";
            }

            wait_for_clients.count_down();
            // std::cerr << "SERVER CONNECTION: wait_for_clients.count_down(1) -> " << wait_for_clients.remaining() <<
            // "\n";
            co_return;
        };
        co_await server_scheduler->schedule();

        coro::latch            wait_for_clients{connections};
        coro::net::tls::server server{
            server_scheduler,
            std::make_shared<coro::net::tls::context>(
                "cert.pem", coro::net::tls::tls_file_type::pem, "key.pem", coro::net::tls::tls_file_type::pem)};

        listening++;

        while (accepted.load(std::memory_order::acquire) < connections)
        {
            auto pstatus = co_await server.poll(std::chrono::milliseconds{1});
            if (pstatus == coro::poll_status::event)
            {
                auto c = co_await server.accept();
                if (c.socket().is_valid())
                {
                    accepted.fetch_add(1, std::memory_order::release);
                    server_scheduler->spawn(make_on_connection_task(std::move(c), wait_for_clients));
                }
            }
        }

        // std::cerr << "SERVER: co_await wait_for_clients\n";
        co_await wait_for_clients;
        // std::cerr << "SERVER: co_await wait_for_clients DONE\n";
        co_return;
    };

    coro::mutex                                   histogram_mutex;
    std::map<std::chrono::milliseconds, uint64_t> g_histogram;

    auto client_scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{
        .pool               = coro::thread_pool::options{},
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool});
    auto make_client_task = [](std::shared_ptr<coro::io_scheduler>            client_scheduler,
                               const std::string&                             msg,
                               std::atomic<uint64_t>&                         clients_completed,
                               std::map<std::chrono::milliseconds, uint64_t>& g_histogram,
                               coro::mutex&                                   histogram_mutex) -> coro::task<void>
    {
        co_await client_scheduler->schedule();
        {
            std::map<std::chrono::milliseconds, uint64_t> histogram;
            coro::net::tls::client                        client{
                client_scheduler, std::make_shared<coro::net::tls::context>(coro::net::tls::verify_peer_t::no)};

            auto cstatus = co_await client.connect();
            REQUIRE(cstatus == coro::net::tls::connection_status::connected);

            std::string in(64, '\0');
            auto        closed = false;

            for (size_t i = 1; i <= messages_per_connection; ++i)
            {
                auto req_start = std::chrono::steady_clock::now();

                auto [sstatus, remaining] = co_await client.send(msg);
                // std::cerr << "CLIENT: send() -> " << msg << "\n";
                REQUIRE(sstatus == coro::net::tls::send_status::ok);
                REQUIRE(remaining.empty());

                // std::cerr << "CLIENT: recv()\n";
                auto [rstatus, rspan] = co_await client.recv(in, {std::chrono::seconds{30}});
                // std::cerr << "CLIENT: rstatus =" << coro::net::tls::to_string(rstatus) << "\n";
                switch (rstatus)
                {
                    case coro::net::tls::recv_status::ok:
                    {
                        REQUIRE(rstatus == coro::net::tls::recv_status::ok);
                        auto data = std::string_view{rspan.begin(), rspan.end()};
                        REQUIRE(data.length() > 0);
                        // std::cerr << "CLIENT: recv() -> " << data << "\n";
                    }
                    break;
                    case coro::net::tls::recv_status::closed:
                        // std::cerr << "CLIENT: closed\n";
                        REQUIRE(rspan.empty());
                        closed = true;
                        break;
                    case coro::net::tls::recv_status::want_read:
                    {
                        // std::cerr << "CLIENT: want_read\n";
                    }
                        continue;
                    case coro::net::tls::recv_status::want_write:
                    {
                        // std::cerr << "CLIENT: want_write\n";
                    }
                        continue;
                    case ::coro::net::tls::recv_status::timeout:
                        std::cerr << "client.recv() timeout, closing\n";
                        closed = true;
                        break;
                    default:
                        // std::cerr << "CLIENT: error (closing)\n";
                        closed = true;
                        break;
                }

                if (closed)
                {
                    break;
                }

                auto req_stop = std::chrono::steady_clock::now();
                histogram[std::chrono::duration_cast<std::chrono::milliseconds>(req_stop - req_start)]++;
            }

            {
                // std::cerr << "CLIENT: writing histogram\n";
                auto lock = co_await histogram_mutex.lock();
                for (auto [ms, count] : histogram)
                {
                    g_histogram[ms] += count;
                }
            }

            auto cc = clients_completed.fetch_add(1);
            std::cerr << "CLIENT: clients_completed: " << (cc + 1) << "\n";
        }

        co_return;
    };

    auto start = sc::now();

    // Create the server to accept incoming tcp connections.
    auto server_thread =
        std::thread{[&]() { coro::sync_wait(make_server_task(server_scheduler, listening, accepted)); }};

    // The server can take a small bit of time to start up, if we don't wait for it to notify then
    // the first few connections can easily fail to connect causing this test to fail.
    while (listening < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    auto client_thread =
        std::thread{[&]()
                    {
                        std::vector<coro::task<void>> tasks{};
                        for (size_t i = 0; i < connections; ++i)
                        {
                            tasks.emplace_back(make_client_task(
                                client_scheduler, msg, clients_completed, g_histogram, histogram_mutex));
                        }
                        coro::sync_wait(coro::when_all(std::move(tasks)));
                    }};

    std::cerr << "joining client thread...\n";
    client_thread.join();
    std::cerr << "joining server thread...\n";
    server_thread.join();
    std::cerr << "all coroutines joined\n";

    auto stop = sc::now();
    print_stats("benchmark tls::client and tls::server thread_pool", ops, start, stop);

    for (const auto& [ms, count] : g_histogram)
    {
        std::cerr << ms.count() << " : " << count << "\n";
    }
}
    #endif // LIBCORO_FEATURE_TLS
#endif     // LIBCORO_FEATURE_NETWORKING

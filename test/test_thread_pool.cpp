#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <array>
#include <barrier>
#include <iostream>

TEST_CASE("thread_pool", "[thread_pool]")
{
    std::cerr << "[thread_pool]\n\n";
}

TEST_CASE("thread_pool one worker one task", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{1});

    auto func = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        co_await tp->schedule(); // Schedule this coroutine on the scheduler.
        co_return 42;
    };

    auto result = coro::sync_wait(func(tp));
    REQUIRE(result == 42);
}

TEST_CASE("thread_pool one worker many tasks tuple", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{1});

    auto f = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        co_await tp->schedule(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    auto tasks = coro::sync_wait(coro::when_all(f(tp), f(tp), f(tp), f(tp), f(tp)));
    REQUIRE(std::tuple_size<decltype(tasks)>() == 5);

    uint64_t counter{0};
    std::apply([&counter](auto&&... t) -> void { ((counter += t.return_value()), ...); }, tasks);

    REQUIRE(counter == 250);
}

TEST_CASE("thread_pool one worker many tasks vector", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{1});

    auto f = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        co_await tp->schedule(); // Schedule this coroutine on the scheduler.
        co_return 50;
    };

    std::vector<coro::task<uint64_t>> input_tasks;
    input_tasks.emplace_back(f(tp));
    input_tasks.emplace_back(f(tp));
    input_tasks.emplace_back(f(tp));

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
    auto                        tp         = coro::thread_pool::make_unique();

    auto make_task = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        co_await tp->schedule();
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
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{1});

    auto f1 = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
    {
        co_await tp->schedule();

        auto f2 = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<uint64_t>
        {
            co_await tp->schedule();
            co_return 5;
        };

        co_return 1 + co_await f2(tp);
    };

    REQUIRE(coro::sync_wait(f1(tp)) == 6);
}

TEST_CASE("thread_pool shutdown", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{1});

    auto f = [](std::unique_ptr<coro::thread_pool>& tp) -> coro::task<bool>
    {
        try
        {
            co_await tp->schedule();
        }
        catch (...)
        {
            co_return true;
        }
        co_return false;
    };

    tp->shutdown();

    REQUIRE(coro::sync_wait(f(tp)) == true);
}

TEST_CASE("thread_pool event jump threads", "[thread_pool]")
{
    // This test verifies that the thread that sets the event ends up executing every waiter on the event

    auto tp1 = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    auto tp2 = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    coro::event e{};

    auto make_tp1_task = [](std::unique_ptr<coro::thread_pool>& tp1, coro::event& e) -> coro::task<void>
    {
        co_await tp1->schedule();
        auto before_thread_id = std::this_thread::get_id();
        std::cerr << "before event thread_id = " << before_thread_id << "\n";
        co_await e;
        auto after_thread_id = std::this_thread::get_id();
        std::cerr << "after event thread_id = " << after_thread_id << "\n";

        REQUIRE(before_thread_id != after_thread_id);

        co_return;
    };

    auto make_tp2_task = [](std::unique_ptr<coro::thread_pool>& tp2, coro::event& e) -> coro::task<void>
    {
        co_await tp2->schedule();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        std::cerr << "setting event\n";
        e.set();
        co_return;
    };

    coro::sync_wait(coro::when_all(make_tp1_task(tp1, e), make_tp2_task(tp2, e)));
}

TEST_CASE("thread_pool high cpu usage when threadcount is greater than the number of tasks", "[thread_pool]")
{
    // https://github.com/jbaldwin/libcoro/issues/262
    // This test doesn't really trigger any error conditions but was reported via
    // an issue that the thread_pool threads not doing work would spin on the CPU
    // if there were less tasks running than threads in the pool.
    // This was due to using m_size instead of m_queue.size() causing the threads
    // that had no work to go into a spin trying to acquire work.

    auto wait_for_task = [](std::unique_ptr<coro::thread_pool>& tp, std::chrono::seconds delay) -> coro::task<>
    {
        auto sleep_for_task = [](std::chrono::seconds duration) -> coro::task<int64_t>
        {
            std::this_thread::sleep_for(duration);
            co_return duration.count();
        };

        co_await tp->schedule();
        for (int i = 0; i < 5; ++i)
        {
            co_await sleep_for_task(delay);
            std::cout << std::chrono::system_clock::now().time_since_epoch().count() << " wait for " << delay.count()
                      << "seconds\n";
        }
        co_return;
    };

    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 3});
    coro::sync_wait(
        coro::when_all(wait_for_task(tp, std::chrono::seconds{1}), wait_for_task(tp, std::chrono::seconds{3})));
}

TEST_CASE("issue-287", "[thread_pool]")
{
    const int ITERATIONS = 200000;

    std::atomic<uint32_t> g_count = 0;
    auto                  tp      = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    auto task = [](std::atomic<uint32_t>& count) -> coro::task<void>
    {
        count++;
        co_return;
    };

    for (int i = 0; i < ITERATIONS; ++i)
    {
        REQUIRE(tp->spawn_detached(task(g_count)));
    }

    tp->shutdown();

    std::cerr << "g_count = \t" << g_count.load() << std::endl;
    REQUIRE(g_count.load() == ITERATIONS);
}

TEST_CASE("thread_pool::spawn", "[thread_pool]")
{
    auto                  tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 2});
    std::atomic<uint64_t> counter{0};

    auto make_task = [](std::atomic<uint64_t>& counter, uint64_t amount) -> coro::task<void>
    {
        counter += amount;
        co_return;
    };

    REQUIRE(tp->spawn_detached(make_task(counter, 1)));
    REQUIRE(tp->spawn_detached(make_task(counter, 2)));
    REQUIRE(tp->spawn_detached(make_task(counter, 3)));

    tp->shutdown();

    REQUIRE(counter == 6);
}

TEST_CASE("thread_pool::schedule(task)", "[thread_pool]")
{
    auto            tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    uint64_t        counter{0};
    std::thread::id coroutine_tid;

    auto make_task = [](uint64_t value, std::thread::id& coroutine_id) -> coro::task<uint64_t>
    {
        coroutine_id = std::this_thread::get_id();
        co_return value;
    };

    auto main_tid = std::this_thread::get_id();

    counter += coro::sync_wait(tp->schedule(make_task(53, coroutine_tid)));

    REQUIRE(counter == 53);
    REQUIRE(main_tid != coroutine_tid);
}

TEST_CASE("thread_pool::schedule(task) manually resume returned task", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    auto long_task = []() -> coro::task<bool>
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        co_return true;
    };

    auto task = long_task();
    tp->resume(task.handle());

    REQUIRE(!task.is_ready());

    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE(task.is_ready());
    auto result = coro::sync_wait(task);
    REQUIRE(result == true);
}

TEST_CASE("thread_pool::spawn_joinable", "[thread_pool]")
{
    auto                  tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    std::atomic<uint64_t> counter{0};
    coro::event           e1{};

    auto long_task = [](std::atomic<uint64_t>& c, coro::event& e1) -> coro::task<void>
    {
        ++c;
        e1.set();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        ++c;
        co_return;
    };

    auto join_task = tp->spawn_joinable(long_task(counter, e1));
    coro::sync_wait(e1);
    REQUIRE(counter.load() == 1);
    coro::sync_wait(join_task);
    REQUIRE(counter.load() == 2);
}

namespace
{
struct live_body
{
    std::atomic<std::size_t>& count;

    live_body(std::atomic<std::size_t>& count, std::atomic<std::size_t>& peak) : count(count)
    {
        auto current = count.fetch_add(1, std::memory_order::relaxed) + 1;
        auto seen    = peak.load(std::memory_order::relaxed);
        while (current > seen && !peak.compare_exchange_weak(seen, current, std::memory_order::relaxed)) {}
    }

    ~live_body() { count.fetch_sub(1, std::memory_order::relaxed); }
};

auto fork_join_fib(
    coro::thread_pool& tp, std::atomic<std::size_t>& live, std::atomic<std::size_t>& peak, std::size_t n)
    -> coro::task<std::size_t>
{
    live_body body{live, peak};
    if (n < 2)
    {
        co_return n;
    }

    co_await tp.schedule();
    auto [x, y] = co_await coro::when_all(fork_join_fib(tp, live, peak, n - 1), fork_join_fib(tp, live, peak, n - 2));
    co_return x.return_value() + y.return_value();
}
} // namespace

TEST_CASE("thread_pool runs recursive fork-join work depth first", "[thread_pool]")
{
    // https://github.com/jbaldwin/libcoro/issues/446
    // Breadth-first expansion keeps many coroutine bodies alive; depth-first work stays bounded by tree depth.
    constexpr std::size_t depth        = 16;
    const auto            thread_count = GENERATE(1u, 4u);
    auto                  tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = thread_count});
    std::atomic<std::size_t> live{0};
    std::atomic<std::size_t> peak{0};

    REQUIRE(coro::sync_wait(fork_join_fib(*tp, live, peak, depth)) == 987);
    REQUIRE(live.load() == 0);
    REQUIRE(peak.load() <= 2 * thread_count * depth);
}

TEST_CASE("thread_pool wakes after repeated idle transitions", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 4});
    auto work = [](coro::thread_pool& tp, std::size_t value) -> coro::task<std::size_t>
    {
        co_await tp.schedule();
        co_return value;
    };

    for (std::size_t round = 0; round < 256; ++round)
    {
        while (!tp->empty())
        {
            std::this_thread::yield();
        }
        auto [a, b, c, d] = coro::sync_wait(coro::when_all(
            work(*tp, round), work(*tp, round + 1), work(*tp, round + 2), work(*tp, round + 3)));
        REQUIRE(a.return_value() + b.return_value() + c.return_value() + d.return_value() == 4 * round + 6);
    }
}

TEST_CASE("thread_pool wakes peers for locally forked work", "[thread_pool]")
{
    constexpr std::size_t workers = 4;
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = workers});
    std::barrier rendezvous{static_cast<std::ptrdiff_t>(workers)};
    auto child = [](coro::thread_pool& tp, std::barrier<>& rendezvous) -> coro::task<int>
    {
        co_await tp.schedule();
        rendezvous.arrive_and_wait();
        co_return 1;
    };
    auto root = [&]() -> coro::task<int>
    {
        co_await tp->schedule();
        auto [a, b, c, d] = co_await coro::when_all(
            child(*tp, rendezvous), child(*tp, rendezvous), child(*tp, rendezvous), child(*tp, rendezvous));
        co_return a.return_value() + b.return_value() + c.return_value() + d.return_value();
    };
    REQUIRE(coro::sync_wait(root()) == workers);
}

TEST_CASE("thread_pool rejects publication after shutdown", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 2});
    auto operation = tp->schedule();
    tp->shutdown();

    auto delayed = [&]() -> coro::task<void>
    {
        co_await operation;
    };
    REQUIRE_THROWS_AS(coro::sync_wait(delayed()), std::runtime_error);

    bool ran{false};
    auto work = [](bool& ran) -> coro::task<void>
    {
        ran = true;
        co_return;
    };
    auto task = work(ran);
    std::array<std::coroutine_handle<>, 2> handles{task.handle(), nullptr};
    REQUIRE_FALSE(tp->resume(task.handle()));
    REQUIRE(tp->resume(handles) == 0);
    REQUIRE_FALSE(tp->spawn_detached(work(ran)));
    REQUIRE_FALSE(ran);
    REQUIRE(tp->empty());
}

TEST_CASE("~thread_pool", "[thread_pool]")
{
    std::cerr << "[~thread_pool]\n\n";
}

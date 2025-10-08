#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

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
    auto tp = coro::thread_pool::make_unique();

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
    auto tp              = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});

    auto task = [](std::atomic<uint32_t>& count) -> coro::task<void>
    {
        count++;
        co_return;
    };

    for (int i = 0; i < ITERATIONS; ++i)
    {
        REQUIRE(tp->spawn(task(g_count)));
    }

    tp->shutdown();

    std::cerr << "g_count = \t" << g_count.load() << std::endl;
    REQUIRE(g_count.load() == ITERATIONS);
}

TEST_CASE("thread_pool::spawn", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 2});
    std::atomic<uint64_t> counter{0};

    auto make_task = [](std::atomic<uint64_t>& counter, uint64_t amount) -> coro::task<void>
    {
        counter += amount;
        co_return;
    };

    REQUIRE(tp->spawn(make_task(counter, 1)));
    REQUIRE(tp->spawn(make_task(counter, 2)));
    REQUIRE(tp->spawn(make_task(counter, 3)));

    tp->shutdown();

    REQUIRE(counter == 6);
}

TEST_CASE("thread_pool::schedule(task)", "[thread_pool]")
{
    auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
    uint64_t          counter{0};
    std::thread::id   coroutine_tid;

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

// TEST_CASE("thread_pool destruction", "[thread_pool]")
// {
//     std::weak_ptr<coro::thread_pool> weakref;
//     {
//         auto tp = coro::thread_pool::make_unique(coro::thread_pool::options{.thread_count = 1});
//         weakref = tp;
//         REQUIRE(weakref.lock() != nullptr);
//     }
//     REQUIRE(weakref.lock() == nullptr);
// }

TEST_CASE("~thread_pool", "[thread_pool]")
{
    std::cerr << "[~thread_pool]\n\n";
}

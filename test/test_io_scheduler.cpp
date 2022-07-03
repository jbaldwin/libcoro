#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std::chrono_literals;

TEST_CASE("io_scheduler schedule single task", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_task = [&]() -> coro::task<uint64_t>
    {
        co_await s.schedule();
        co_return 42;
    };

    auto value = coro::sync_wait(make_task());
    REQUIRE(value == 42);
    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler submit mutiple tasks", "[io_scheduler]")
{
    constexpr std::size_t         n = 1000;
    std::atomic<uint64_t>         counter{0};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(n);
    coro::io_scheduler s{};

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        counter++;
        co_return;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        tasks.emplace_back(make_task());
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(counter == n);

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler task with multiple events", "[io_scheduler]")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    coro::event e1;
    coro::event e2;
    coro::event e3;

    auto make_wait_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        co_await e1;
        counter++;
        co_await e2;
        counter++;
        co_await e3;
        counter++;
        co_return;
    };

    auto make_set_task = [&](coro::event& e) -> coro::task<void>
    {
        co_await s.schedule();
        e.set();
    };

    coro::sync_wait(coro::when_all(make_wait_task(), make_set_task(e1), make_set_task(e2), make_set_task(e3)));

    REQUIRE(counter == 3);

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler task with read poll", "[io_scheduler]")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_poll_read_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(status == coro::poll_status::event);
        co_return;
    };

    auto make_poll_write_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        uint64_t value{42};
        auto     unused = write(trigger_fd, &value, sizeof(value));
        (void)unused;
        co_return;
    };

    coro::sync_wait(coro::when_all(make_poll_read_task(), make_poll_write_task()));

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read poll with timeout", "[io_scheduler]")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_poll_read_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        // Poll with a timeout but don't timeout.
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read, 50ms);
        REQUIRE(status == coro::poll_status::event);
        co_return;
    };

    auto make_poll_write_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        uint64_t value{42};
        auto     unused = write(trigger_fd, &value, sizeof(value));
        (void)unused;
        co_return;
    };

    coro::sync_wait(coro::when_all(make_poll_read_task(), make_poll_write_task()));

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read poll timeout", "[io_scheduler]")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        // Poll with a timeout and timeout.
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read, 10ms);
        REQUIRE(status == coro::poll_status::timeout);
        co_return;
    };

    coro::sync_wait(make_task());

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
    close(trigger_fd);
}

// TODO: This probably requires a TCP socket?
// TEST_CASE("io_scheduler task with read poll closed socket", "[io_scheduler]")
// {
//     auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
//     coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options { .thread_count = 1 }}};

//     auto make_poll_task = [&]() -> coro::task<void> {
//         co_await s.schedule();
//         auto status = co_await s.poll(trigger_fd, coro::poll_op::read, 1000ms);
//         REQUIRE(status == coro::poll_status::closed);
//         co_return;
//     };

//     auto make_close_task = [&]() -> coro::task<void> {
//         co_await s.schedule();
//         std::this_thread::sleep_for(100ms);
//         // shutdown(trigger_fd, SHUT_RDWR);
//         close(trigger_fd);
//         co_return;
//     };

//     coro::sync_wait(coro::when_all(make_poll_task(), make_close_task()));

//     s.shutdown();
//     REQUIRE(s.empty());
// }

TEST_CASE("io_scheduler separate thread resume", "[io_scheduler]")
{
    coro::io_scheduler s1{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};
    coro::io_scheduler s2{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    coro::event e{};

    auto make_s1_task = [&]() -> coro::task<void>
    {
        co_await s1.schedule();
        auto tid = std::this_thread::get_id();
        co_await e;

        // This coroutine will hop to the other scheduler's single thread upon resuming.
        REQUIRE_FALSE(tid == std::this_thread::get_id());

        co_return;
    };

    auto make_s2_task = [&]() -> coro::task<void>
    {
        co_await s2.schedule();
        // Wait a bit to be sure the wait on 'e' in the other scheduler is done first.
        std::this_thread::sleep_for(10ms);
        e.set();
        co_return;
    };

    coro::sync_wait(coro::when_all(make_s1_task(), make_s2_task()));

    s1.shutdown();
    REQUIRE(s1.empty());
    s2.shutdown();
    REQUIRE(s2.empty());
}

TEST_CASE("io_scheduler separate thread resume spawned thread", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        coro::event e{};

        auto tid = std::this_thread::get_id();

        // Normally this thread is probably already running for real world use cases, but in general
        // the 3rd party function api will be set, they should have "user data" void* or ability
        // to capture variables via lambdas for on complete callbacks, here we mimic an on complete
        // callback by capturing the hande.
        std::thread third_party_thread(
            [&e, &s]() -> void
            {
                // mimic some expensive computation
                // Resume the coroutine back onto the scheduler, not this background thread.
                e.set(s);
            });
        third_party_thread.detach();

        // Wait on the handle until the 3rd party service is completed.
        co_await e;
        REQUIRE(tid == std::this_thread::get_id());
    };

    coro::sync_wait(make_task());

    s.shutdown();
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler separate thread resume with return", "[io_scheduler]")
{
    constexpr uint64_t expected_value{1337};
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    coro::event           start_service{};
    coro::event           service_done{};
    std::atomic<uint64_t> output;

    std::thread service{
        [&]() -> void
        {
            while (!start_service.is_set())
            {
                std::this_thread::sleep_for(1ms);
            }

            output = expected_value;
            service_done.set(s);
        }};

    auto third_party_service = [&](int multiplier) -> coro::task<uint64_t>
    {
        start_service.set();
        co_await service_done;
        co_return output* multiplier;
    };

    auto make_task = [&]() -> coro::task<void>
    {
        co_await s.schedule();

        int      multiplier{5};
        uint64_t value = co_await third_party_service(multiplier);
        REQUIRE(value == (expected_value * multiplier));
    };

    coro::sync_wait(make_task());

    service.join();
    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler with basic task", "[io_scheduler]")
{
    constexpr std::size_t expected_value{5};
    coro::io_scheduler    s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto add_data = [&](uint64_t val) -> coro::task<int>
    {
        co_await s.schedule();
        co_return val;
    };

    auto func = [&]() -> coro::task<int>
    {
        co_await s.schedule();

        auto output_tasks = co_await coro::when_all(add_data(1), add_data(1), add_data(1), add_data(1), add_data(1));

        int counter{0};
        std::apply([&counter](auto&&... tasks) -> void { ((counter += tasks.return_value()), ...); }, output_tasks);

        co_return counter;
    };

    auto counter = coro::sync_wait(func());

    REQUIRE(counter == expected_value);

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler scheduler_after", "[io_scheduler]")
{
    constexpr std::chrono::milliseconds wait_for{50};
    std::atomic<uint64_t>               counter{0};
    std::thread::id                     tid;

    auto func = [&](coro::io_scheduler& s, std::chrono::milliseconds amount) -> coro::task<void>
    {
        co_await s.schedule_after(amount);
        ++counter;
        // Make sure schedule after context switches into the worker thread.
        REQUIRE(tid == std::this_thread::get_id());
        co_return;
    };

    {
        coro::io_scheduler s{coro::io_scheduler::options{
            .pool = coro::thread_pool::options{
                .thread_count = 1, .on_thread_start_functor = [&](std::size_t) { tid = std::this_thread::get_id(); }}}};
        auto               start = std::chrono::steady_clock::now();
        coro::sync_wait(func(s, 0ms));
        auto stop     = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

        REQUIRE(counter == 1);
        REQUIRE(duration < wait_for);
        std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
        s.shutdown();
        std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
        REQUIRE(s.empty());
    }

    {
        coro::io_scheduler s{coro::io_scheduler::options{
            .pool = coro::thread_pool::options{
                .thread_count = 1, .on_thread_start_functor = [&](std::size_t) { tid = std::this_thread::get_id(); }}}};

        auto start = std::chrono::steady_clock::now();
        coro::sync_wait(func(s, wait_for));
        auto stop     = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

        REQUIRE(counter == 2);
        REQUIRE(duration >= wait_for);
        std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
        s.shutdown();
        std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
        REQUIRE(s.empty());
    }
}

TEST_CASE("io_scheduler schedule_at", "[io_scheduler]")
{
    // Because schedule_at() will take its own time internally the wait_for might be off by a bit.
    constexpr std::chrono::milliseconds epsilon{3};
    constexpr std::chrono::milliseconds wait_for{50};
    std::atomic<uint64_t>               counter{0};
    std::thread::id                     tid;

    coro::io_scheduler s{coro::io_scheduler::options{
        .pool = coro::thread_pool::options{
            .thread_count = 1, .on_thread_start_functor = [&](std::size_t) { tid = std::this_thread::get_id(); }}}};

    auto func = [&](std::chrono::steady_clock::time_point time) -> coro::task<void>
    {
        co_await s.schedule_at(time);
        ++counter;
        REQUIRE(tid == std::this_thread::get_id());
        co_return;
    };

    {
        auto start = std::chrono::steady_clock::now();
        coro::sync_wait(func(std::chrono::steady_clock::now() + wait_for));
        auto stop     = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

        REQUIRE(counter == 1);
        REQUIRE(duration >= (wait_for - epsilon));
    }

    {
        auto start = std::chrono::steady_clock::now();
        coro::sync_wait(func(std::chrono::steady_clock::now()));
        auto stop     = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

        REQUIRE(counter == 2);
        REQUIRE(duration <= 10ms); // Just verify its less than the wait_for time period.
    }

    {
        auto start = std::chrono::steady_clock::now();
        coro::sync_wait(func(std::chrono::steady_clock::now() - 1s));
        auto stop     = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

        REQUIRE(counter == 3);
        REQUIRE(duration <= 10ms);
    }
}

TEST_CASE("io_scheduler yield", "[io_scheduler]")
{
    std::thread::id    tid;
    coro::io_scheduler s{coro::io_scheduler::options{
        .pool = coro::thread_pool::options{
            .thread_count = 1, .on_thread_start_functor = [&](std::size_t) { tid = std::this_thread::get_id(); }}}};

    auto func = [&]() -> coro::task<void>
    {
        REQUIRE(tid != std::this_thread::get_id());
        co_await s.schedule();
        REQUIRE(tid == std::this_thread::get_id());
        co_await s.yield(); // this is really a thread pool function but /shrug
        REQUIRE(tid == std::this_thread::get_id());
        co_return;
    };

    coro::sync_wait(func());

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler yield_for", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    const std::chrono::milliseconds wait_for{50};

    auto make_task = [&]() -> coro::task<std::chrono::milliseconds>
    {
        co_await s.schedule();
        auto start = std::chrono::steady_clock::now();
        co_await s.yield_for(wait_for);
        co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    };

    auto duration = coro::sync_wait(make_task());
    REQUIRE(duration >= wait_for);

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler yield_until", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    // Because yield_until() takes its own time internally the wait_for might be off by a bit.
    const std::chrono::milliseconds epsilon{3};
    const std::chrono::milliseconds wait_for{50};

    auto make_task = [&]() -> coro::task<std::chrono::milliseconds>
    {
        co_await s.schedule();
        auto start = std::chrono::steady_clock::now();
        co_await s.yield_until(start + wait_for);
        co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    };

    auto duration = coro::sync_wait(make_task());
    REQUIRE(duration >= (wait_for - epsilon));

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler multipler event waiters", "[io_scheduler]")
{
    const constexpr std::size_t total{10};
    coro::event                 e{};
    coro::io_scheduler          s{};

    auto func = [&]() -> coro::task<uint64_t>
    {
        co_await e;
        co_return 1;
    };

    auto spawn = [&]() -> coro::task<void>
    {
        co_await s.schedule();
        std::vector<coro::task<uint64_t>> tasks;
        for (size_t i = 0; i < total; ++i)
        {
            tasks.emplace_back(func());
        }

        auto results = co_await coro::when_all(std::move(tasks));

        uint64_t counter{0};
        for (const auto& task : results)
        {
            counter += task.return_value();
        }
        REQUIRE(counter == total);
    };

    auto release = [&]() -> coro::task<void>
    {
        co_await s.schedule_after(10ms);
        e.set(s);
    };

    coro::sync_wait(coro::when_all(spawn(), release()));

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler self generating coroutine (stack overflow check)", "[io_scheduler]")
{
    const constexpr std::size_t total{1'000'000};
    uint64_t                    counter{0};
    coro::io_scheduler          s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    std::vector<coro::task<void>> tasks;
    tasks.reserve(total);

    auto func = [&](auto f) -> coro::task<void>
    {
        co_await s.schedule();
        ++counter;

        if (counter % total == 0)
        {
            co_return;
        }

        // co_await f(f) _will_ stack overflow since each coroutine links to its parent, by storing
        // each new invocation into the vector they are not linked, but we can make sure the scheduler
        // doesn't choke on this many tasks being scheduled.
        tasks.emplace_back(f(f));
        tasks.back().resume();
        co_return;
    };

    coro::sync_wait(func(func));

    while (tasks.size() < total - 1)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(tasks.size() == total - 1);

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler manual process events thread pool", "[io_scheduler]")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{coro::io_scheduler::options{
        .thread_strategy = coro::io_scheduler::thread_strategy_t::manual,
        .pool            = coro::thread_pool::options{
                       .thread_count = 1,
        }}};

    std::atomic<bool> polling{false};

    auto make_poll_read_task = [&]() -> coro::task<void>
    {
        std::cerr << "poll task start s.size() == " << s.size() << "\n";
        co_await s.schedule();
        polling = true;
        std::cerr << "poll task polling s.size() == " << s.size() << "\n";
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(status == coro::poll_status::event);
        std::cerr << "poll task exiting s.size() == " << s.size() << "\n";
        co_return;
    };

    auto make_poll_write_task = [&]() -> coro::task<void>
    {
        std::cerr << "write task start s.size() == " << s.size() << "\n";
        co_await s.schedule();
        uint64_t value{42};
        std::cerr << "write task writing s.size() == " << s.size() << "\n";
        auto unused = write(trigger_fd, &value, sizeof(value));
        (void)unused;
        std::cerr << "write task exiting s.size() == " << s.size() << "\n";
        co_return;
    };

    auto poll_task  = make_poll_read_task();
    auto write_task = make_poll_write_task();

    poll_task.resume(); // get to co_await s.poll();
    while (!polling)
    {
        std::this_thread::sleep_for(10ms);
    }

    write_task.resume();

    while (s.process_events(100ms) > 0)
        ;

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler manual process events inline", "[io_scheduler]")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{coro::io_scheduler::options{
        .thread_strategy    = coro::io_scheduler::thread_strategy_t::manual,
        .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_inline}};

    auto make_poll_read_task = [&]() -> coro::task<void>
    {
        std::cerr << "poll task start s.size() == " << s.size() << "\n";
        co_await s.schedule();
        std::cerr << "poll task polling s.size() == " << s.size() << "\n";
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(status == coro::poll_status::event);
        std::cerr << "poll task exiting s.size() == " << s.size() << "\n";
        co_return;
    };

    auto make_poll_write_task = [&]() -> coro::task<void>
    {
        std::cerr << "write task start s.size() == " << s.size() << "\n";
        co_await s.schedule();
        uint64_t value{42};
        std::cerr << "write task writing s.size() == " << s.size() << "\n";
        auto unused = write(trigger_fd, &value, sizeof(value));
        (void)unused;
        std::cerr << "write task exiting s.size() == " << s.size() << "\n";
        co_return;
    };

    auto poll_task  = make_poll_read_task();
    auto write_task = make_poll_write_task();

    // Start the tasks by scheduling them into the io scheduler.
    poll_task.resume();
    write_task.resume();

    // Now process them to completion.
    while (true)
    {
        auto remaining = s.process_events(100ms);
        std::cerr << "remaining " << remaining << "\n";
        if (remaining == 0)
        {
            break;
        }
    };

    std::cerr << "io_scheduler.size() before shutdown = " << s.size() << "\n";
    s.shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << s.size() << "\n";
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task throws", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto func = [&]() -> coro::task<uint64_t>
    {
        co_await s.schedule();
        throw std::runtime_error{"I always throw."};
        co_return 42;
    };

    REQUIRE_THROWS(coro::sync_wait(func()));
}

TEST_CASE("io_scheduler task throws after resume", "[io_scheduler]")
{
    coro::io_scheduler s{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};

    auto make_thrower = [&]() -> coro::task<bool>
    {
        co_await s.schedule();
        std::cerr << "Throwing task is doing some work...\n";
        co_await s.yield();
        throw std::runtime_error{"I always throw."};
        co_return true;
    };

    REQUIRE_THROWS(coro::sync_wait(make_thrower()));
}
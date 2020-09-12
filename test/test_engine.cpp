#include "catch.hpp"

#include <coro/coro.hpp>

#include <thread>
#include <chrono>
#include <sys/eventfd.h>
#include <unistd.h>

TEST_CASE("engine submit single task")
{
    using namespace std::chrono_literals;
    using task_type = coro::engine::task_type;

    coro::engine eng{};

    std::atomic<uint64_t> counter{0};

    auto task1 = [&]() -> task_type { counter++; co_return; }();

    eng.submit_task(std::move(task1));
    while(counter != 1)
    {
        std::this_thread::sleep_for(10ms);
    }

    REQUIRE(eng.size() == 0);
}

TEST_CASE("engine submit mutiple tasks")
{
    using namespace std::chrono_literals;
    using task_type = coro::engine::task_type;

    coro::engine eng{};

    std::atomic<uint64_t> counter{0};

    auto func = [&]() -> task_type { counter++; co_return; };

    auto task1 = func();
    auto task2 = func();
    auto task3 = func();

    eng.submit_task(std::move(task1));
    eng.submit_task(std::move(task2));
    eng.submit_task(std::move(task3));
    while(counter != 3)
    {
        std::this_thread::sleep_for(10ms);
    }

    // Make sure every task is also destroyed since they have completed.
    REQUIRE(eng.size() == 0);
}

TEST_CASE("engine task in a task")
{
    using namespace std::chrono_literals;
    using task_type = coro::engine::task_type;

    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    std::atomic<uint64_t> output{0};

    coro::engine eng{};

    std::atomic<coro::engine::task_id_type> task_id{0};

    auto task = [&]() -> task_type
    {
        std::cerr << "task begin\n";
        std::cerr << "eng.await(" << task_id << ", " << trigger_fd << ", read)\n";
        eng.await(task_id.load(), trigger_fd, coro::await_op::read);
        std::cerr << "co_await std::suspend_always()\n";
        co_await std::suspend_always();

        std::cerr << "task resume()\n";
        uint64_t v{0};
        read(trigger_fd, &v, sizeof(v));
        output = v;
        std::cerr << "task end\n";
    }();

    task_id = eng.submit_task(std::move(task));

    uint64_t value{42};
    write(trigger_fd, &value, sizeof(value));

    while(eng.size() > 0)
    {
        std::this_thread::sleep_for(10ms);
    }

    REQUIRE(output == value);
}
#include "catch.hpp"

#include <coro/coro.hpp>

#include <thread>
#include <chrono>

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
#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>


TEST_CASE("task hello world")
{
    using task_type = coro::task<std::string>;

    auto h = []() -> task_type { co_return "Hello"; }();
    auto w = []() -> task_type { co_return "World"; }();

    REQUIRE(h.promise().result().empty());
    REQUIRE(w.promise().result().empty());

    h.resume(); // task suspends immediately
    w.resume();

    REQUIRE(h.is_ready());
    REQUIRE(w.is_ready());

    auto w_value = std::move(w).promise().result();

    REQUIRE(h.promise().result() == "Hello");
    REQUIRE(w_value == "World");
    REQUIRE(w.promise().result().empty());
}

TEST_CASE("task void")
{
    using namespace std::chrono_literals;
    using task_type = coro::task<>;

    auto t = []() -> task_type {
        std::this_thread::sleep_for(10ms);
        co_return;
    }();
    t.resume();

    REQUIRE(t.is_ready());
}

TEST_CASE("task exception thrown")
{
    using task_type = coro::task<std::string>;

    std::string throw_msg = "I'll be reached";

    auto task = [&]() -> task_type {
        throw std::runtime_error(throw_msg);
        co_return "I'll never be reached";
    }();

    task.resume();

    REQUIRE(task.is_ready());

    bool thrown{false};
    try
    {
        auto value = task.promise().result();
    }
    catch(const std::exception& e)
    {
        thrown = true;
        REQUIRE(e.what() == throw_msg);
    }

    REQUIRE(thrown);
}

TEST_CASE("task in a task")
{
    auto outer_task = []() -> coro::task<>
    {
        auto inner_task = []() -> coro::task<int>
        {
            std::cerr << "inner_task start\n";
            std::cerr << "inner_task stop\n";
            co_return 42;
        };

        std::cerr << "outer_task start\n";
        auto v = co_await inner_task();
        REQUIRE(v == 42);
        std::cerr << "outer_task stop\n";
    }();

    outer_task.resume(); // all tasks start suspend, kick it off.

    REQUIRE(outer_task.is_ready());
}

TEST_CASE("task in a task in a task")
{
    auto task1 = []() -> coro::task<>
    {
        std::cerr << "task1 start\n";
        auto task2 = []() -> coro::task<int>
        {
            std::cerr << "\ttask2 start\n";
            auto task3 = []() -> coro::task<int>
            {
                std::cerr << "\t\ttask3 start\n";
                std::cerr << "\t\ttask3 stop\n";
                co_return 3;
            };

            auto v2 = co_await task3();
            REQUIRE(v2 == 3);

            std::cerr << "\ttask2 stop\n";
            co_return 2;
        };

        auto v1 = co_await task2();
        REQUIRE(v1 == 2);

        std::cerr << "task1 stop\n";
    }();

    task1.resume(); // all tasks start suspended, kick it off.

    REQUIRE(task1.is_ready());
}

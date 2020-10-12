#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>


TEST_CASE("task hello world")
{
    using task_type = coro::task<std::string>;

    auto h = []() -> task_type { co_return "Hello"; }();
    auto w = []() -> task_type { co_return "World"; }();

    REQUIRE(h.promise().return_value().empty());
    REQUIRE(w.promise().return_value().empty());

    h.resume(); // task suspends immediately
    w.resume();

    REQUIRE(h.is_ready());
    REQUIRE(w.is_ready());

    auto w_value = std::move(w).promise().return_value();

    REQUIRE(h.promise().return_value() == "Hello");
    REQUIRE(w_value == "World");
    REQUIRE(w.promise().return_value().empty());
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
        auto value = task.promise().return_value();
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

TEST_CASE("task multiple suspends return void")
{
    auto task = []() -> coro::task<void>
    {
        co_await std::suspend_always{};
        co_await std::suspend_never{};
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_return;
    }();

    task.resume(); // initial suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // first internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // second internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // third internal suspend
    REQUIRE(task.is_ready());
}

TEST_CASE("task multiple suspends return integer")
{
    auto task = []() -> coro::task<int>
    {
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_await std::suspend_always{};
        co_return 11;
    }();

    task.resume(); // initial suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // first internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // second internal suspend
    REQUIRE_FALSE(task.is_ready());

    task.resume(); // third internal suspend
    REQUIRE(task.is_ready());
    REQUIRE(task.promise().return_value() == 11);
}

TEST_CASE("task resume from promise to coroutine handles of different types")
{
    auto task1 = [&]() -> coro::task<int>
    {
        std::cerr << "Task ran\n";
        co_return 42;
    }();

    auto task2 = [&]() -> coro::task<void>
    {
        std::cerr << "Task 2 ran\n";
        co_return;
    }();

    // task.resume();  normal method of resuming

    std::vector<std::coroutine_handle<>> handles;

    handles.emplace_back(std::coroutine_handle<coro::task<int>::promise_type>::from_promise(task1.promise()));
    handles.emplace_back(std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task2.promise()));

    auto& coro_handle1 = handles[0];
    coro_handle1.resume();
    auto& coro_handle2 = handles[1];
    coro_handle2.resume();

    REQUIRE(task1.is_ready());
    REQUIRE(coro_handle1.done());
    REQUIRE(task1.promise().return_value() == 42);

    REQUIRE(task2.is_ready());
    REQUIRE(coro_handle2.done());
}

TEST_CASE("task throws")
{
    auto task = []() -> coro::task<int>
    {
        throw std::runtime_error{"I always throw."};
        co_return 42;
    }();

    task.resume();
    REQUIRE(task.is_ready());
    REQUIRE_THROWS_AS(task.promise().return_value(), std::runtime_error);
}
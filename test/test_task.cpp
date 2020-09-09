#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>


TEST_CASE("hello world task")
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

TEST_CASE("void task")
{
    using namespace std::chrono_literals;
    using task_type = coro::task<void>;

    auto t = []() -> task_type {
        std::this_thread::sleep_for(10ms);
        co_return;
    }();
    t.resume();

    REQUIRE(t.is_ready());
}

TEST_CASE("Exception thrown")
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

TEST_CASE("Task in a task")
{
    auto inner_task = []() -> coro::task<int> {
        std::cerr << "inner_task start\n";
        std::cerr << "inner_task stop\n";
        co_return 42;
    };
    auto outer_task = [&]() -> coro::task<> {
        std::cerr << "outer_task start\n";
        auto v = co_await inner_task();
        REQUIRE(v == 42);
        std::cerr << "outer_task stop\n";
    }();

    outer_task.resume();
}

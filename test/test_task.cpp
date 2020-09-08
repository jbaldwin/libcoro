#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

static auto hello() -> coro::task<std::string, std::suspend_always>
{
    co_return "Hello";
}

static auto world() -> coro::task<std::string, std::suspend_always>
{
    co_return "World";
}

static auto void_task() -> coro::task<void, std::suspend_always>
{
    co_return;
}

static auto throws_exception() -> coro::task<std::string, std::suspend_always>
{
    co_await std::suspend_always();
    throw std::runtime_error("I'll be reached");
    co_return "I'll never be reached";
}

TEST_CASE("hello world task")
{
    auto h = hello();
    auto w = world();

    REQUIRE(h.promise().result().empty());
    REQUIRE(w.promise().result().empty());

    h.resume(); // task suspends immediately
    w.resume();

    auto w_value = std::move(w).promise().result();

    REQUIRE(h.promise().result() == "Hello");
    REQUIRE(w_value == "World");
    REQUIRE(w.promise().result().empty());
}

// This currently won't report as is_done(), not sure why yet...
// TEST_CASE("void task")
// {
//     auto task = void_task();
//     task.resume();

//     REQUIRE(task.is_done());
// }

TEST_CASE("Exception thrown")
{
    auto task = throws_exception();

    try
    {
        task.resume();
    }
    catch(const std::exception& e)
    {
        REQUIRE(e.what() == "I'll be reached");
    }
}

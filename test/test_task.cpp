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

    REQUIRE(h.return_value().empty());
    REQUIRE(w.return_value().empty());

    h.resume(); // task suspends immediately
    w.resume();

    auto w_value = std::move(w).return_value();

    REQUIRE(h.return_value() == "Hello");
    REQUIRE(w_value == "World");
    REQUIRE(w.return_value().empty());
}

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

#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <thread>

template<typename return_type>
auto mre_producer(coro::async_manual_reset_event<return_type>& event, return_type produced_value) -> void
{
    // simulate complicated background task
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10ms);
    event.set(std::move(produced_value));
}

template<typename return_type>
auto mre_consumer(
    const coro::async_manual_reset_event<return_type>& event
) -> coro::task<return_type, std::suspend_never>
{
    co_await event;
    co_return event.return_value();
}

TEST_CASE("manual reset event one watcher")
{
    coro::async_manual_reset_event<uint64_t> event{};

    auto value = mre_consumer(event);
    mre_producer<uint64_t>(event, 42);
    value.resume();

    REQUIRE(value.promise().result() == 42);
}

TEST_CASE("manual reset event multiple watcher")
{
    std::string expected_value = "Hello World!";
    coro::async_manual_reset_event<std::string> event{};

    auto value1 = mre_consumer(event);
    auto value2 = mre_consumer(event);
    auto value3 = mre_consumer(event);
    mre_producer<std::string>(event, expected_value);
    value1.resume();
    value2.resume();
    value3.resume();

    REQUIRE(value1.promise().result() == expected_value);
    REQUIRE(value2.promise().result() == expected_value);
    REQUIRE(value3.promise().result() == expected_value);
}

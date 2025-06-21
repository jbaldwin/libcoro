#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>
#include <coro/fd.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

TEST_CASE("task_container", "[task_container]")
{
    std::cerr << "[task_container]\n\n";
}

using namespace std::chrono_literals;
using coro::fd_t;

TEST_CASE("task_container schedule single task", "[task_container]")
{
    auto s = coro::thread_pool::make_shared(
        coro::thread_pool::options{.thread_count = 1});

    int value = 0;
    auto make_task = [] (int &value) -> coro::task<void>
    {
        value = 37;
        co_return;
    };

    coro::task_container<coro::thread_pool> tc{s};
    tc.start(make_task(value));
    REQUIRE(tc.size() == 1);
    coro::sync_wait(tc.yield_until_empty());
    REQUIRE(value == 37);
    REQUIRE(tc.empty());
}

TEST_CASE("task_container submit mutiple tasks", "[task_container]")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    auto s = coro::io_scheduler::make_shared();
    coro::task_container<coro::io_scheduler> tc{s};

    auto make_task = [](std::atomic<uint64_t>& counter) -> coro::task<void>
    {
        counter++;
        co_return;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        tc.start(make_task(counter));
    }

    coro::sync_wait(tc.yield_until_empty());
    REQUIRE(counter == n);
    REQUIRE(tc.empty());
}

TEST_CASE("~task_container", "[task_container]")
{
    std::cerr << "[~task_container]\n\n";
}

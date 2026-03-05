#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <iostream>

TEST_CASE("thread_pool_ws", "[thread_pool_ws]")
{
    std::cerr << "[thread_pool_ws]\n\n";
}

TEST_CASE("thread_pool_ws simple testing", "[thread_pool_ws]")
{
    coro::thread_pool_ws tp{coro::thread_pool_ws::options{.worker_count = 8}};
    coro::mutex          m{};

    auto make_task = [&tp, &m](int task_id) -> coro::task<void>
    {
        for (size_t i = 0; i < 50'000; ++i)
        {
            co_await tp.schedule();
            std::stringstream ss;
            ss << task_id << " iteration " << i << " thread id =[" << std::this_thread::get_id() << "]\n";
            // std::cout << ss.str();
        }
        co_return;
    };

    size_t                        iterations{1024};
    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(tp.schedule(make_task(i)));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));
}

TEST_CASE("~thread_pool_ws", "[thread_pool_ws]")
{
    std::cerr << "[~thread_pool_ws]\n\n";
}

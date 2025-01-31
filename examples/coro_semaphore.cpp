#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Have more threads/tasks than the semaphore will allow for at any given point in time.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 8}};
    coro::semaphore   semaphore{2};

    auto make_rate_limited_task =
        [](coro::thread_pool& tp, coro::semaphore& semaphore, uint64_t task_num) -> coro::task<void>
    {
        co_await tp.schedule();

        // This will only allow 2 tasks through at any given point in time, all other tasks will
        // await the resource to be available before proceeding.
        auto result = co_await semaphore.acquire();
        if (result == coro::semaphore::acquire_result::acquired)
        {
            std::cout << task_num << ", ";
            semaphore.release();
        }
        else
        {
            std::cout << task_num << " failed to acquire semaphore [" << coro::semaphore::to_string(result) << "],";
        }
        co_return;
    };

    const size_t                  num_tasks{100};
    std::vector<coro::task<void>> tasks{};
    for (size_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_rate_limited_task(tp, semaphore, i));
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));
}

#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a scheduler to execute all tasks in parallel and also so we can
    // suspend a task to act like a timeout event.
    auto scheduler = coro::io_scheduler::make_shared();

    // This task will behave like a long running task and will produce a valid result.
    auto make_long_running_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                                     std::chrono::milliseconds           execution_time) -> coro::task<int64_t>
    {
        // Schedule the task to execute in parallel.
        co_await scheduler->schedule();
        // Fake doing some work...
        co_await scheduler->yield_for(execution_time);
        // Return the result.
        co_return 1;
    };

    auto make_timeout_task = [](std::shared_ptr<coro::io_scheduler> scheduler) -> coro::task<int64_t>
    {
        // Schedule a timer to be fired so we know the task timed out.
        co_await scheduler->schedule_after(std::chrono::milliseconds{100});
        co_return -1;
    };

    // Example showing the long running task completing first.
    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{50}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        std::cout << "result = " << result << "\n";
    }

    // Example showing the long running task timing out.
    {
        std::vector<coro::task<int64_t>> tasks{};
        tasks.emplace_back(make_long_running_task(scheduler, std::chrono::milliseconds{500}));
        tasks.emplace_back(make_timeout_task(scheduler));

        auto result = coro::sync_wait(coro::when_any(std::move(tasks)));
        std::cout << "result = " << result << "\n";
    }
}

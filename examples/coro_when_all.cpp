#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a thread pool to execute all the tasks in parallel.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 4}};
    // Create the task we want to invoke multiple times and execute in parallel on the thread pool.
    auto twice = [](coro::thread_pool& tp, uint64_t x) -> coro::task<uint64_t>
    {
        co_await tp.schedule(); // Schedule onto the thread pool.
        co_return x + x;        // Executed on the thread pool.
    };

    // Make our tasks to execute, tasks can be passed in via a std::ranges::range type or var args.
    std::vector<coro::task<uint64_t>> tasks{};
    for (std::size_t i = 0; i < 5; ++i)
    {
        tasks.emplace_back(twice(tp, i + 1));
    }

    // Synchronously wait on this thread for the thread pool to finish executing all the tasks in parallel.
    auto results = coro::sync_wait(coro::when_all(std::move(tasks)));
    for (auto& result : results)
    {
        // If your task can throw calling return_value() will either return the result or re-throw the exception.
        try
        {
            std::cout << result.return_value() << "\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << '\n';
        }
    }

    // Use var args instead of a container as input to coro::when_all.
    auto square = [](coro::thread_pool& tp, double x) -> coro::task<double>
    {
        co_await tp.schedule();
        co_return x* x;
    };

    // Var args allows you to pass in tasks with different return types and returns
    // the result as a std::tuple.
    auto tuple_results = coro::sync_wait(coro::when_all(square(tp, 1.1), twice(tp, 10)));

    auto first  = std::get<0>(tuple_results).return_value();
    auto second = std::get<1>(tuple_results).return_value();

    std::cout << "first: " << first << " second: " << second << "\n";
}

#include <coro/coro.hpp>
#include <iostream>
#include <random>

int main()
{
    coro::thread_pool tp{coro::thread_pool::options{
        // By default all thread pools will create its thread count with the
        // std::thread::hardware_concurrency() as the number of worker threads in the pool,
        // but this can be changed via this thread_count option.  This example will use 4.
        .thread_count = 4,
        // Upon starting each worker thread an optional lambda callback with the worker's
        // index can be called to make thread changes, perhaps priority or change the thread's
        // name.
        .on_thread_start_functor = [](std::size_t worker_idx) -> void
        { std::cout << "thread pool worker " << worker_idx << " is starting up.\n"; },
        // Upon stopping each worker thread an optional lambda callback with the worker's
        // index can b called.
        .on_thread_stop_functor = [](std::size_t worker_idx) -> void
        { std::cout << "thread pool worker " << worker_idx << " is shutting down.\n"; }}};

    auto primary_task = [](coro::thread_pool& tp) -> coro::task<uint64_t>
    {
        auto offload_task = [](coro::thread_pool& tp, uint64_t child_idx) -> coro::task<uint64_t>
        {
            // Start by scheduling this offload worker task onto the thread pool.
            co_await tp.schedule();
            // Now any code below this schedule() line will be executed on one of the thread pools
            // worker threads.

            // Mimic some expensive task that should be run on a background thread...
            std::random_device              rd;
            std::mt19937                    gen{rd()};
            std::uniform_int_distribution<> d{0, 1};

            size_t calculation{0};
            for (size_t i = 0; i < 1'000'000; ++i)
            {
                calculation += d(gen);

                // Lets be nice and yield() to let other coroutines on the thread pool have some cpu
                // time.  This isn't necessary but is illustrated to show how tasks can cooperatively
                // yield control at certain points of execution.  Its important to never call the
                // std::this_thread::sleep_for() within the context of a coroutine, that will block
                // and other coroutines which are ready for execution from starting, always use yield()
                // or within the context of a coro::io_scheduler you can use yield_for(amount).
                if (i == 500'000)
                {
                    std::cout << "Task " << child_idx << " is yielding()\n";
                    co_await tp.yield();
                }
            }
            co_return calculation;
        };

        const size_t                      num_children{10};
        std::vector<coro::task<uint64_t>> child_tasks{};
        child_tasks.reserve(num_children);
        for (size_t i = 0; i < num_children; ++i)
        {
            child_tasks.emplace_back(offload_task(tp, i));
        }

        // Wait for the thread pool workers to process all child tasks.
        auto results = co_await coro::when_all(std::move(child_tasks));

        // Sum up the results of the completed child tasks.
        size_t calculation{0};
        for (const auto& task : results)
        {
            calculation += task.return_value();
        }
        co_return calculation;
    };

    auto result = coro::sync_wait(primary_task(tp));
    std::cout << "calculated thread pool result = " << result << "\n";
}

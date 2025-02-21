#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Complete worker tasks faster on a thread pool, using the io_scheduler version so the worker
    // tasks can yield for a specific amount of time to mimic difficult work.  The pool is only
    // setup with a single thread to showcase yield_for().
    auto tp = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    // This task will wait until the given latch setters have completed.
    auto make_latch_task = [](coro::latch& l) -> coro::task<void>
    {
        // It seems like the dependent worker tasks could be created here, but in that case it would
        // be superior to simply do: `co_await coro::when_all(tasks);`
        // It is also important to note that the last dependent task will resume the waiting latch
        // task prior to actually completing -- thus the dependent task's frame could be destroyed
        // by the latch task completing before it gets a chance to finish after calling resume() on
        // the latch task!

        std::cout << "latch task is now waiting on all children tasks...\n";
        co_await l;
        std::cout << "latch task dependency tasks completed, resuming.\n";
        co_return;
    };

    // This task does 'work' and counts down on the latch when completed.  The final child task to
    // complete will end up resuming the latch task when the latch's count reaches zero.
    auto make_worker_task = [](std::shared_ptr<coro::io_scheduler> tp, coro::latch& l, int64_t i) -> coro::task<void>
    {
        // Schedule the worker task onto the thread pool.
        co_await tp->schedule();
        std::cout << "worker task " << i << " is working...\n";
        // Do some expensive calculations, yield to mimic work...!  Its also important to never use
        // std::this_thread::sleep_for() within the context of coroutines, it will block the thread
        // and other tasks that are ready to execute will be blocked.
        co_await tp->yield_for(std::chrono::milliseconds{i * 20});
        std::cout << "worker task " << i << " is done, counting down on the latch\n";
        l.count_down();
        co_return;
    };

    const int64_t                 num_tasks{5};
    coro::latch                   l{num_tasks};
    std::vector<coro::task<void>> tasks{};

    // Make the latch task first so it correctly waits for all worker tasks to count down.
    tasks.emplace_back(make_latch_task(l));
    for (int64_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_worker_task(tp, l, i));
    }

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all(std::move(tasks)));
}

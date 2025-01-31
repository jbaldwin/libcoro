#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // This lambda will create a coro::task that returns a unit64_t.
    // It can be invoked many times with different arguments.
    auto make_task_inline = [](uint64_t x) -> coro::task<uint64_t> { co_return x + x; };

    // This will block the calling thread until the created task completes.
    // Since this task isn't scheduled on any coro::thread_pool or coro::io_scheduler
    // it will execute directly on the calling thread.
    auto result = coro::sync_wait(make_task_inline(5));
    std::cout << "Inline Result = " << result << "\n";

    // We'll make a 1 thread coro::thread_pool to demonstrate offloading the task's
    // execution to another thread.  We'll pass the thread pool as a parameter so
    // the task can be scheduled.
    // Note that you will need to guarantee the thread pool outlives the coroutine.
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 1}};

    auto make_task_offload = [](coro::thread_pool& tp, uint64_t x) -> coro::task<uint64_t>
    {
        co_await tp.schedule(); // Schedules execution on the thread pool.
        co_return x + x;        // This will execute on the thread pool.
    };

    // This will still block the calling thread, but it will now offload to the
    // coro::thread_pool since the coroutine task is immediately scheduled.
    result = coro::sync_wait(make_task_offload(tp, 10));
    std::cout << "Offload Result = " << result << "\n";
}

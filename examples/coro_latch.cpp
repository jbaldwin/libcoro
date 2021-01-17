#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // This task will wait until the given latch setters have completed.
    auto make_latch_task = [](coro::latch& l) -> coro::task<void> {
        std::cout << "latch task is now waiting on all children tasks...\n";
        co_await l;
        std::cout << "latch task children tasks completed, resuming.\n";
        co_return;
    };

    // This task does 'work' and counts down on the latch when completed.  The final child task to
    // complete will end up resuming the latch task when the latch's count reaches zero.
    auto make_worker_task = [](coro::latch& l, int64_t i) -> coro::task<void> {
        std::cout << "work task " << i << " is working...\n";
        std::cout << "work task " << i << " is done, counting down on the latch\n";
        l.count_down();
        co_return;
    };

    // It is important to note that the latch task must not 'own' the worker tasks within its
    // coroutine stack frame because the final worker task thread will execute the latch task upon
    // setting the latch counter to zero.  This means that:
    //     1) final worker task calls count_down() => 0
    //     2) resume execution of latch task to its next suspend point or completion, IF completed
    //        then this coroutine's stack frame is destroyed!
    //     3) final worker task continues exection
    // If the latch task 'own's the worker task objects then they will destruct prior to step (3)
    // if the latch task completes on that resume, and it will be attempting to execute an already
    // destructed coroutine frame.
    // This example correctly has the latch task and all its waiting tasks on the same scope/frame
    // to avoid this issue.
    const int64_t                 num_tasks{5};
    coro::latch                   l{num_tasks};
    std::vector<coro::task<void>> tasks{};

    // Make the latch task first so it correctly waits for all worker tasks to count down.
    tasks.emplace_back(make_latch_task(l));
    for (int64_t i = 1; i <= num_tasks; ++i)
    {
        tasks.emplace_back(make_worker_task(l, i));
    }

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all_awaitable(tasks));
}

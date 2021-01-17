#include <coro/coro.hpp>
#include <iostream>

int main()
{
    coro::event e;

    // This task will wait until the given event has been set before advancings
    auto make_wait_task = [](const coro::event& e, int i) -> coro::task<int> {
        std::cout << "task " << i << " is waiting on the event...\n";
        co_await e;
        std::cout << "task " << i << " event triggered, now resuming.\n";
        co_return i;
    };

    // This task will trigger the event allowing all waiting tasks to proceed.
    auto make_set_task = [](coro::event& e) -> coro::task<void> {
        std::cout << "set task is triggering the event\n";
        e.set();
        co_return;
    };

    // Synchronously wait until all the tasks are completed, this is intentionally
    // starting the first 3 wait tasks prior to the final set task.
    coro::sync_wait(
        coro::when_all_awaitable(make_wait_task(e, 1), make_wait_task(e, 2), make_wait_task(e, 3), make_set_task(e)));
}

#include <coro/coro.hpp>
#include <iostream>

int main()
{
    coro::event e;

    // These tasks will wait until the given event has been set before advancing.
    auto make_wait_task = [](const coro::event& e, uint64_t i) -> coro::task<void>
    {
        std::cout << "task " << i << " is waiting on the event...\n";
        co_await e;
        std::cout << "task " << i << " event triggered, now resuming.\n";
        co_return;
    };

    // This task will trigger the event allowing all waiting tasks to proceed.
    auto make_set_task = [](coro::event& e) -> coro::task<void>
    {
        std::cout << "set task is triggering the event\n";
        e.set();
        co_return;
    };

    // Given more than a single task to synchronously wait on, use when_all() to execute all the
    // tasks concurrently on this thread and then sync_wait() for them all to complete.
    coro::sync_wait(coro::when_all(make_wait_task(e, 1), make_wait_task(e, 2), make_wait_task(e, 3), make_set_task(e)));
}

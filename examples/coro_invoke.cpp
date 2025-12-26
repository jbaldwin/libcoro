#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto tp = coro::thread_pool::make_unique();

    int a = 1;
    int b = 2;
    int c = 3;

    auto make_task_with_captures = [&tp, &c](int d, int e) -> coro::task<int>
    {
        // Mimic a suspension so lambda captures would normally be dangling/destroyed.
        co_await tp->yield();
        co_return c + d + e;
    };

    // This is bad form, the captures will be dangling after `co_await tp->yield();`.
    // coro::sync_wait(make_task_with_captures(a, b));

    // This is good form, the coro::invoke will create a stable coroutine frame for the lambda captures.
    std::cout << coro::sync_wait(coro::invoke(make_task_with_captures, a, b));
}

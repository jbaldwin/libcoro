#include "catch.hpp"

#include <coro/coro.hpp>

#include <iostream>

// TEST_CASE("thread_pool one worker, one task")
// {
//     coro::thread_pool tp{1};

//     auto func = [&tp]() -> coro::task<uint64_t>
//     {
//         std::cerr << "func()\n";
//         co_await tp.schedule().value(); // Schedule this coroutine on the scheduler.
//         std::cerr << "func co_return 42\n";
//         co_return 42;
//     };

//     std::cerr << "coro::sync_wait(func()) start\n";
//     coro::sync_wait(func());
//     std::cerr << "coro::sync_wait(func()) end\n";
// }
// #include "catch.hpp"

// #include <coro/coro.hpp>

// auto execute_task() -> coro::engine::task
// {
//     std::cerr << "engine task successfully executed\n";
//     co_return;
// }

// TEST_CASE("engine submit one request")
// {
//     coro::engine eng{};

//     eng.submit_task(execute_task());
// }
#pragma once

#include "coro/task.hpp"

namespace coro
{

template<typename awaitable_functor>
auto sync_wait(awaitable_functor&& awaitable) -> decltype(auto)
{
    auto task = awaitable();
    while(!task.is_ready())
    {
        task.resume();
    }
    return task.promise().result();
}

} // namespace coro

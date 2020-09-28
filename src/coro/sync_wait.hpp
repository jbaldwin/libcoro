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

template<typename return_type>
auto sync_wait_task(coro::task<return_type> task) -> return_type
{
    while(!task.is_ready())
    {
        task.resume();
    }

    if constexpr (std::is_same_v<return_type, void>)
    {
        return;
    }
    else
    {
        return task.promise().result();
    }
}

} // namespace coro

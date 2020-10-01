#pragma once

#include "coro/task.hpp"
#include "coro/scheduler.hpp"

namespace coro
{

template<typename task_type>
auto sync_wait(task_type&& task) -> decltype(auto)
{
    while(!task.is_ready())
    {
        task.resume();
    }
    return task.promise().result();
}

template<typename ... tasks>
auto sync_wait_all(tasks&& ...awaitables) -> void
{
    scheduler s{ scheduler::options {
        .reserve_size = sizeof...(awaitables),
        .thread_strategy = scheduler::thread_strategy_t::manual }
    };

    (s.schedule(std::move(awaitables)), ...);

    while(s.process_events() > 0) ;
}

} // namespace coro

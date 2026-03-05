#include "coro/thread_pool_ws.hpp"

#include <iostream>

namespace coro
{
thread_local std::optional<uint32_t> thread_pool_ws::m_thread_pool_queue_idx{std::nullopt};

thread_pool_ws::~thread_pool_ws()
{
    for (auto& worker : m_workers)
    {
        worker.request_stop();
        worker.join();
    }
}

auto thread_pool_ws::schedule() -> schedule_operation
{
    return schedule_operation(*this);
}

auto thread_pool_ws::execute(std::stop_token st, uint32_t idx) -> void
{
    m_thread_pool_queue_idx = {idx};
    auto&  thread_queue     = *m_queues[idx];
    size_t spin_counter{0};
    while (!st.stop_requested())
    {
        bool had_tasks = drain_thread_queue(thread_queue);
        had_tasks |= try_steal(idx);
        had_tasks |= drain_global_queue(thread_queue);

        if (had_tasks)
        {
            spin_counter = 0;
            continue;
        }

        if (++spin_counter <= 100)
        {
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

auto thread_pool_ws::drain_thread_queue(riften::Deque<schedule_operation*>& queue) -> bool
{
    bool had_task{false};
    while (!queue.empty())
    {
        auto op = queue.pop();
        if (op.has_value())
        {
            op.value()->m_awaiting_coroutine.resume();
            had_task = true;
        }
    }
    return had_task;
}

auto thread_pool_ws::try_steal(uint32_t my_idx) -> bool
{
    bool had_tasks{false};
    auto queue_size = m_queues.size();
    for (size_t i = 0; i < queue_size; ++i)
    {
        if (i == my_idx)
        {
            continue;
        }

        had_tasks |= drain_peer_queue(*m_queues[i]);
    }
    return had_tasks;
}

auto thread_pool_ws::drain_peer_queue(riften::Deque<schedule_operation*>& queue) -> bool
{
    bool had_task{false};
    while (!queue.empty())
    {
        auto op = queue.steal();
        if (op.has_value())
        {
            op.value()->m_awaiting_coroutine.resume();
            had_task = true;
        }
    }
    return had_task;
}

auto thread_pool_ws::drain_global_queue(riften::Deque<schedule_operation*>& queue) -> bool
{
    if (m_global_queue.load(std::memory_order::acquire) != nullptr)
    {
        auto* head_op = detail::awaiter_list_pop_all(m_global_queue);
        if (head_op != nullptr)
        {
            while (head_op != nullptr)
            {
                auto* next_op = head_op->m_next;
                queue.emplace(head_op);
                head_op = next_op;
            }
        }

        // If another worker beat us to it notify there are tasks to try and steal.
        return true;
    }

    return false;
}

} // namespace coro

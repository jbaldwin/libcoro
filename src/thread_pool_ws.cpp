#include "coro/thread_pool_ws.hpp"
#include "coro/task_group.hpp"

#include <iostream>

namespace coro
{
namespace detail
{
static auto make_spawned_joinable_wait_task(std::unique_ptr<coro::task_group<coro::thread_pool_ws>> group_ptr)
    -> coro::task<void>
{
    co_await *group_ptr;
    co_return;
}

} // namespace detail

thread_local std::optional<uint32_t> thread_pool_ws::m_thread_pool_queue_idx{std::nullopt};

thread_pool_ws::worker_info::worker_info(thread_pool_ws& tp, uint32_t i)
{
    // Start the thread after all of the worker_info fields have been initialized.
    m_thread = std::thread([&tp, i]() -> void { tp.execute(i); });
}

thread_pool_ws::thread_pool_ws(options opts, private_constructor) : m_opts(std::move(opts))
{
    m_workers.reserve(m_opts.thread_count);
}

auto thread_pool_ws::make_unique(options opts) -> std::unique_ptr<thread_pool_ws>
{
    auto tp = std::make_unique<thread_pool_ws>(std::move(opts), private_constructor{});

    for (uint32_t i = 0; i < tp->m_opts.thread_count; ++i)
    {
        tp->m_workers.emplace_back(std::make_unique<worker_info>(*tp, i));
    }

    return tp;
}

thread_pool_ws::~thread_pool_ws()
{
    shutdown();
}

thread_pool_ws::schedule_operation::schedule_operation(thread_pool_ws& tp) noexcept : m_thread_pool(tp)
{
    m_thread_pool.m_queue_size.fetch_add(1, std::memory_order::release);
    m_thread_pool.m_size.fetch_add(1, std::memory_order::release);
}

auto thread_pool_ws::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_awaiting_coroutine = awaiting_coroutine;
    // See if we are running on a thread pool worker to enqueue locally.
    auto& idx = thread_pool_ws::m_thread_pool_queue_idx;
    if (idx.has_value())
    {
        m_thread_pool.m_workers[idx.value()]->m_queue.emplace(this);
    }
    else
    {
        detail::awaiter_list_push(m_thread_pool.m_global_queue, this);
    }

    m_thread_pool.try_wake_worker();
}

auto thread_pool_ws::schedule() -> schedule_operation
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        throw std::runtime_error("coro::thread_pool_ws is shutting down, unable to schedule new tasks.");
    }

    return schedule_operation(*this);
}

auto thread_pool_ws::spawn_detached(coro::task<void> user_task) -> bool
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        return false;
    }

    auto wrapper_task = detail::make_task_self_deleting(std::move(user_task));
    return resume(wrapper_task.handle());
}

auto thread_pool_ws::spawn_joinable(coro::task<void> user_task) -> coro::task<void>
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        throw std::runtime_error("coro::thread_pool_ws is shutting down, unable to spawn new tasks.");
    }

    auto group_ptr = std::make_unique<coro::task_group<coro::thread_pool_ws>>(this, std::move(user_task));
    return detail::make_spawned_joinable_wait_task(std::move(group_ptr));
}

auto thread_pool_ws::resume(std::coroutine_handle<> handle) noexcept -> bool
{
    if (handle == nullptr || handle.done())
    {
        return false;
    }

    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        return false;
    }

    auto* op        = new schedule_operation{*this};
    op->m_allocated = true;
    op->await_suspend(handle);
    return true;
}

auto thread_pool_ws::shutdown() noexcept -> void
{
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        {
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.notify_all();
        }

        for (auto& worker : m_workers)
        {
            if (worker->m_thread.joinable())
            {
                worker->m_thread.join();
            }
        }
    }
}

auto thread_pool_ws::execute(uint32_t idx) -> void
{
    m_thread_pool_queue_idx = {idx};

    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    auto&  info         = m_workers[idx];
    auto&  thread_queue = info->m_queue;
    size_t spin_counter{0};
    while (!m_shutdown_requested.load(std::memory_order::acquire))
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

        // Go to sleep until a task is scheduled.
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        m_sleeping.fetch_add(1, std::memory_order::release);
        m_wait_cv.wait(
            lk,
            [&]() {
                return m_queue_size.load(std::memory_order::acquire) > 0 ||
                       m_shutdown_requested.load(std::memory_order::acquire);
            });
    }

    while (m_size.load(std::memory_order::acquire) > 0)
    {
        bool had_tasks = drain_thread_queue(thread_queue);
        had_tasks |= try_steal(idx);
        had_tasks |= drain_global_queue(thread_queue);

        // If there were no tasks left to work on, this thread can exit.
        if (!had_tasks)
        {
            break;
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool_ws::drain_thread_queue(riften::Deque<schedule_operation*>& queue) -> bool
{
    bool had_task{false};
    while (!queue.empty())
    {
        had_task |= resume_task(queue.pop());
    }
    return had_task;
}

auto thread_pool_ws::try_steal(uint32_t my_idx) -> bool
{
    bool had_tasks{false};
    auto queue_size = m_workers.size();
    for (size_t i = 0; i < queue_size; ++i)
    {
        if (i == my_idx)
        {
            continue;
        }

        had_tasks |= drain_peer_queue(m_workers[i]->m_queue);
    }
    return had_tasks;
}

auto thread_pool_ws::drain_peer_queue(riften::Deque<schedule_operation*>& queue) -> bool
{
    bool had_task{false};
    while (!queue.empty())
    {
        had_task |= resume_task(queue.steal());
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

        // 1. If we got the global list we have tasks.
        // 2. If we didn't get the global list we know we can possibly steal.
        // Either way there should be tasks to process.
        return true;
    }

    return false;
}

auto thread_pool_ws::resume_task(std::optional<schedule_operation*> op) -> bool
{
    if (op.has_value())
    {
        m_queue_size.fetch_sub(1, std::memory_order::release);
        auto v = op.value();
        v->m_awaiting_coroutine.resume();
        m_size.fetch_sub(1, std::memory_order::release);
        if (v->m_allocated)
        {
            delete v;
        }
        return true;
    }
    return false;
}

auto thread_pool_ws::try_wake_worker() noexcept -> void
{
    // Attempt to wake a sleeper if there are any.
    auto sleeping = m_sleeping.load(std::memory_order::acquire);
    if (sleeping > 0)
    {
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        sleeping = m_sleeping.load(std::memory_order::acquire);
        if (sleeping == 0)
        {
            return;
        }

        m_sleeping.fetch_sub(1, std::memory_order::release);
        m_wait_cv.notify_one();
    }
}

} // namespace coro

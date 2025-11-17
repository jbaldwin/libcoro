#include "coro/thread_pool.hpp"
#include "coro/detail/awaiter_list.hpp"
#include "coro/detail/task_self_deleting.hpp"

#include <iostream>

namespace coro
{
thread_pool::schedule_operation::schedule_operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{

}

auto thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_awaiting_coroutine = awaiting_coroutine;
    detail::awaiter_list_push(m_thread_pool.m_global_queue, this);
    m_thread_pool.m_queued_size.fetch_add(1, std::memory_order::release);

    // If there are more tasks than awake executors continue to wake them up until none are sleeping or all tasks
    // are accounted for.
    // if (m_thread_pool.should_wakeup_executor())
    {
        m_thread_pool.try_wakeup_executor();
    }
}

thread_pool::thread_pool(options&& opts, private_constructor) : m_opts(opts)
{
    m_threads.reserve(m_opts.thread_count);
    m_executor_state = std::make_unique<executor_state[]>(m_opts.thread_count);
}

auto thread_pool::make_unique(options opts) -> std::unique_ptr<thread_pool>
{
    auto tp = std::make_unique<thread_pool>(std::move(opts), private_constructor{});

    // Initialize the background worker threads once the thread pool is fully constructed
    // so the workers have a full ready object to work with.
    for (uint32_t i = 0; i < tp->m_opts.thread_count; ++i)
    {
        tp->m_threads.emplace_back([tp = tp.get(), i]() { tp->executor(i); });
    }

    return tp;
}

thread_pool::~thread_pool()
{
    shutdown();
}

auto thread_pool::schedule() -> schedule_operation
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
    }

    m_size.fetch_add(1, std::memory_order::release);
    return schedule_operation{*this};
}

auto thread_pool::spawn(coro::task<void>&& task) noexcept -> bool
{
    m_size.fetch_add(1, std::memory_order::release);
    auto wrapper_task = detail::make_task_self_deleting(std::move(task));
    wrapper_task.promise().executor_size(m_size);
    return resume(wrapper_task.handle());
}

auto thread_pool::resume(std::coroutine_handle<> handle) noexcept -> bool
{
    if (handle == nullptr || handle.done() || m_shutdown_requested.load(std::memory_order::acquire))
    {
        return false;
    }

    m_size.fetch_add(1, std::memory_order::release);
    auto* schedule_op = new schedule_operation(*this);
    schedule_op->m_allocated = true;
    schedule_op->await_suspend(handle);
    return true;
}

auto thread_pool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (m_shutdown_requested.exchange(true, std::memory_order::seq_cst) == false)
    {
        for (std::size_t i = 0; i < m_opts.thread_count; ++i)
        {
            auto& state = m_executor_state[i];
            // There is a race condition if we are not holding the lock with the executors
            // to always receive this. std::jthread stop token works without this properly.
            {
                std::unique_lock<std::mutex> lk{state.m_wait_mutex};
                state.m_wait_cv_set = true;
            }
            state.m_wait_cv.notify_one();
        }

        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
}

auto thread_pool::executor(std::size_t idx) -> void
{
    auto& state = m_executor_state[idx];
    auto& local_queue = state.m_queue;;

    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested.
    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        // Try and take tasks from the global queue first.
        auto* op = try_take_global_queue(local_queue);

        // If the global queue is empty try to steal work from other executor threads.
        if (op == nullptr)
        {
            if (m_threads.size() > 1)
            {
                op = try_steal_work(idx);

                // If we stole work see if we can wake other threads up to help.
                try_wakeup_executor();
            }
        }

        // If we managed to get work run until our local queue is empty.
        if (op != nullptr)
        {
            // Drain our local queue.
            do
            {
                resume_coroutine(op);
            } while ((op = detail::awaiter_list_pop(local_queue)) != nullptr);
        }
        else
        {
            // Try and sleep if we cannot find any work.
            try_sleep(state);
        }
    }

    // Process until there are no queued tasks left.
    while (m_queued_size.load(std::memory_order::acquire) > 0)
    {
        auto* op = try_take_global_queue(local_queue);
        if (op == nullptr)
        {
            if (m_threads.size() > 1)
            {
                op = try_steal_work(idx);
            }
        }

        // If there is no global work and no work to steal, stop.
        if (op == nullptr)
        {
            break;
        }

        do
        {
            resume_coroutine(op);
        } while ((op = detail::awaiter_list_pop(local_queue)) != nullptr);
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool::try_take_global_queue(std::atomic<schedule_operation*>& local_queue) -> schedule_operation*
{
    auto* ops = detail::awaiter_list_pop_all(m_global_queue);
    if (ops != nullptr)
    {
        ops = detail::awaiter_list_reverse(ops);
        if (ops->m_next != nullptr)
        {
            detail::awaiter_list_store(local_queue, ops->m_next);
            ops->m_next = nullptr;
        }

    }
    return ops;
}

auto thread_pool::try_steal_work(std::size_t idx) -> schedule_operation*
{
    const std::size_t thread_count = m_threads.size();
    schedule_operation* op{nullptr};
    // for (std::size_t i = (idx + 1) % thread_count; i != idx; i = (i + 1) % thread_count)
    for (std::size_t i = 0; i < thread_count; ++i)
    {
        if (i == idx)
        {
            continue;
        }

        auto& executor_queue = m_executor_state[i].m_queue;
        op = detail::awaiter_list_pop(executor_queue);

        // If we got work resume it.
        if (op != nullptr)
        {
            break;
        }
    }

    return op;
}

auto thread_pool::try_sleep(executor_state& state) -> void
{
    // Notify other threads that this thread can be woken up.
    // m_executors_can_wake_count.fetch_add(1, std::memory_order::seq_cst);

    std::unique_lock<std::mutex> lk{state.m_wait_mutex};
    // It is possible that work has been scheduled while we are trying to go to sleep, now that we hold
    // our state lock check to see if we should actually stay awake based on scheduled work.
    if (try_stay_awake())
    {
        return;
    }

    detail::awaiter_list_push(m_sleeping_executors, &state);

    // Only wake-up if notified we are not sleeping or shutdown has been requested.
    state.m_wait_cv.wait(
        lk,
        [this, &state]()
        {
            return
                       state.m_wait_cv_set.load(std::memory_order::seq_cst)
                    || m_queued_size.load(std::memory_order::acquire) > 0
                    || m_size.load(std::memory_order::acquire) >= m_threads.size()
                    || m_shutdown_requested.load(std::memory_order::acquire);
        });
}

auto thread_pool::try_stay_awake() -> bool
{
    if (m_queued_size.load(std::memory_order::acquire) > 0)
    {
        return true;
    }

    if (m_size.load(std::memory_order::acquire) >= m_threads.size())
    {
        return true;
    }

    return false;
}

auto thread_pool::try_wakeup_executor() -> void
{
    // If there is a sleeping executor wake it up.
    auto* executor = detail::awaiter_list_pop(m_sleeping_executors);
    if (executor != nullptr)
    {
        {
            std::unique_lock<std::mutex> lk{executor->m_wait_mutex};
            executor->m_wait_cv_set.store(true, std::memory_order::seq_cst);
        }
        executor->m_wait_cv.notify_one();
    }
}

auto thread_pool::resume_coroutine(schedule_operation* op) -> void
{
    m_queued_size.fetch_sub(1, std::memory_order::release);
    op->m_awaiting_coroutine.resume();
    m_size.fetch_sub(1, std::memory_order::release);
    if (op->m_allocated)
    {
        delete op;
    }
}

auto thread_pool::should_wakeup_executor() const -> bool
{
    return m_queued_size.load(std::memory_order::acquire) > 0 || m_size.load(std::memory_order::acquire) >= m_threads.size();
}

} // namespace coro

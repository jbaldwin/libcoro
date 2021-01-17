#include "coro/thread_pool.hpp"

namespace coro
{
thread_pool::operation::operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{
}

auto thread_pool::operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_thread_pool.schedule_impl(this);

    // void return on await_suspend suspends the _this_ coroutine, which is now scheduled on the
    // thread pool and returns control to the caller.  They could be sync_wait'ing or go do
    // something else while this coroutine gets picked up by the thread pool.
}

thread_pool::thread_pool(options opts) : m_opts(std::move(opts))
{
    m_threads.reserve(m_opts.thread_count);

    for (uint32_t i = 0; i < m_opts.thread_count; ++i)
    {
        m_threads.emplace_back([this, i](std::stop_token st) { executor(std::move(st), i); });
    }
}

thread_pool::~thread_pool()
{
    shutdown();
}

auto thread_pool::schedule() noexcept -> std::optional<operation>
{
    if (!m_shutdown_requested.load(std::memory_order::relaxed))
    {
        m_size.fetch_add(1, std::memory_order_relaxed);
        return {operation{*this}};
    }

    return std::nullopt;
}

auto thread_pool::shutdown(shutdown_t wait_for_tasks) noexcept -> void
{
    if (!m_shutdown_requested.exchange(true, std::memory_order::release))
    {
        for (auto& thread : m_threads)
        {
            thread.request_stop();
        }

        if (wait_for_tasks == shutdown_t::sync)
        {
            for (auto& thread : m_threads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
        }
    }
}

auto thread_pool::executor(std::stop_token stop_token, std::size_t idx) -> void
{
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    while (true)
    {
        // Wait until the queue has operations to execute or shutdown has been requested.
        {
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.wait(lk, stop_token, [this] { return !m_queue.empty(); });
        }

        // Continue to pull operations from the global queue until its empty.
        while (true)
        {
            operation* op{nullptr};
            {
                std::lock_guard<std::mutex> lk{m_queue_mutex};
                if (!m_queue.empty())
                {
                    op = m_queue.front();
                    m_queue.pop_front();
                }
                else
                {
                    break; // while true, the queue is currently empty
                }
            }

            if (op != nullptr && op->m_awaiting_coroutine != nullptr)
            {
                op->m_awaiting_coroutine.resume();
                m_size.fetch_sub(1, std::memory_order::relaxed);
            }
            else
            {
                break;
            }
        }

        if (stop_token.stop_requested())
        {
            break; // while(true);
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool::schedule_impl(operation* op) noexcept -> void
{
    {
        std::lock_guard<std::mutex> lk{m_queue_mutex};
        m_queue.emplace_back(op);
    }

    m_wait_cv.notify_one();
}

} // namespace coro

#include "coro/thread_pool.hpp"

namespace coro
{

thread_pool::operation::operation(thread_pool& tp) noexcept
    : m_thread_pool(tp)
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

thread_pool::thread_pool(uint32_t thread_count)
{
    m_threads.reserve(thread_count);
    for(uint32_t i = 0; i < thread_count; ++i)
    {
        m_threads.emplace_back([this, i] { run(i); });
    }
}

thread_pool::~thread_pool()
{
    shutdown();

    // If shutdown was called manually by the user with shutdown_t::async then the background
    // worker threads need to be joined upon the thread pool destruction.
    join();
}

auto thread_pool::schedule() noexcept -> std::optional<operation>
{
    if(!m_shutdown_requested.load(std::memory_order::relaxed))
    {
        m_size.fetch_add(1, std::memory_order::relaxed);
        return {operation{*this}};
    }

    return std::nullopt;
}

auto thread_pool::shutdown(shutdown_t wait_for_tasks) -> void
{
    if (!m_shutdown_requested.exchange(true, std::memory_order::release))
    {
        m_queue_cv.notify_all();
        if(wait_for_tasks == shutdown_t::sync)
        {
            join();
        }
    }
}

auto thread_pool::run(uint32_t worker_idx) -> void
{
    while(true)
    {
        // Wait until the queue has operations to execute or shutdown has been requested.
        {
            std::unique_lock<std::mutex> lk{m_queue_cv_mutex};
            m_queue_cv.wait(lk, [this] { return !m_queue.empty() || m_shutdown_requested.load(std::memory_order::relaxed); });
        }

        // Continue to pull operations from the global queue until its empty.
        while(true)
        {
            operation* op{nullptr};
            {
                std::lock_guard<std::mutex> lk{m_queue_mutex};
                if(!m_queue.empty())
                {
                    op = m_queue.front();
                    m_queue.pop_front();
                }
                else
                {
                    break; // while true, the queue is currently empty
                }
            }

            if(op != nullptr && op->m_awaiting_coroutine != nullptr)
            {
                op->m_awaiting_coroutine.resume();
                m_size.fetch_sub(1, std::memory_order::relaxed);
            }
        }

        if(m_shutdown_requested.load(std::memory_order::relaxed))
        {
            break; // while(true);
        }
    }
}

auto thread_pool::join() -> void
{
    for(auto& thread : m_threads)
    {
        thread.join();
    }
    m_threads.clear();
}

auto thread_pool::schedule_impl(operation* op) -> void
{
    {
        std::lock_guard<std::mutex> lk{m_queue_mutex};
        m_queue.emplace_back(op);
    }

    m_queue_cv.notify_one();
}

} // namespace coro

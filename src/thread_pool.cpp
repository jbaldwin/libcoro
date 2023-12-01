#include "coro/thread_pool.hpp"

#include <iostream>

namespace coro
{
thread_pool::operation::operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{
}

auto thread_pool::operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_thread_pool.schedule_impl(m_awaiting_coroutine);

    // void return on await_suspend suspends the _this_ coroutine, which is now scheduled on the
    // thread pool and returns control to the caller.  They could be sync_wait'ing or go do
    // something else while this coroutine gets picked up by the thread pool.
}

thread_pool::thread_pool(options opts) : m_opts(std::move(opts))
{
    m_threads.reserve(m_opts.thread_count);

    for (uint32_t i = 0; i < m_opts.thread_count; ++i)
    {
        m_threads.emplace_back([this, i]() { executor(i); });
    }
}

thread_pool::~thread_pool()
{
    shutdown();
}

auto thread_pool::schedule() -> operation
{
    if (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        m_size.fetch_add(1, std::memory_order::release);
        return operation{*this};
    }

    throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
}

auto thread_pool::resume(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    m_size.fetch_add(1, std::memory_order::release);
    schedule_impl(handle);
}

auto thread_pool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        {
            // There is a race condition if we are not holding the lock with the executors
            // to always receive this.  std::jthread stop token works without this properly.
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.notify_all();
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
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested and the total number of tasks reaches zero.
    while (!m_shutdown_requested.load(std::memory_order::acquire) || m_size.load(std::memory_order::acquire) > 0)
    {
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        m_wait_cv.wait(
            lk,
            [&] {
                return m_size.load(std::memory_order::acquire) > 0 ||
                       m_shutdown_requested.load(std::memory_order::acquire);
            });
        // Process this batch until the queue is empty.
        while (!m_queue.empty())
        {
            auto handle = m_queue.front();
            m_queue.pop_front();

            // Release the lock while executing the coroutine.
            lk.unlock();
            handle.resume();

            m_size.fetch_sub(1, std::memory_order::release);
            lk.lock();
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool::schedule_impl(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    {
        std::scoped_lock lk{m_wait_mutex};
        m_queue.emplace_back(handle);
    }

    m_wait_cv.notify_one();
}

} // namespace coro

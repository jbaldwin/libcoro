#include "coro/thread_pool.hpp"
#include "coro/detail/task_self_deleting.hpp"

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
    std::cerr << "~thread_pool() exit\n";
}

auto thread_pool::schedule() -> operation
{
    m_size.fetch_add(1, std::memory_order::release);
    if (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        return operation{*this};
    }
    else
    {
        m_size.fetch_sub(1, std::memory_order::release);
        throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
    }
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
    if (handle == nullptr || handle.done())
    {
        return false;
    }

    m_size.fetch_add(1, std::memory_order::release);
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        m_size.fetch_sub(1, std::memory_order::release);
        return false;
    }

    schedule_impl(handle);
    return true;
}

auto thread_pool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    std::cerr << "thread_pool::shutdown()\n";
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        {
            // There is a race condition if we are not holding the lock with the executors
            // to always receive this.  std::jthread stop token works without this properly.
            std::cerr << "thread_pool::shutdown() lock()\n";
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            std::cerr << "thread_pool::shutdown() notify_all()\n";
            m_wait_cv.notify_all();
        }

        std::cerr << "thread_pool::shutdown() m_threads.size() = " << m_threads.size() << "\n";
        for (auto& thread : m_threads)
        {
            std::cerr << "thread_pool::shutdown() thread.joinable()\n";
            if (thread.joinable())
            {
                std::cerr << "thread_pool::shutdown() thread.join()\n";
                thread.join();
            }
            else
            {
                std::cerr << "thread_pool::shutdown() thread is not joinable\n";
            }
        }
    }
    std::cerr << "thread_pool::shutdown() return\n";
}

auto thread_pool::executor(std::size_t idx) -> void
{
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested.
    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        m_wait_cv.wait(lk, [&]() { return !m_queue.empty() || m_shutdown_requested.load(std::memory_order::acquire); });

        if (m_queue.empty())
        {
            continue;
        }

        auto handle = m_queue.front();
        m_queue.pop_front();
        lk.unlock();

        // Release the lock while executing the coroutine.
        if (handle == nullptr)
        {
            std::cerr << "handle is nullptr\n";
        }
        else if (handle.done())
        {
            std::cerr << "handle.done() == true\n";
        }
        else
        {
            handle.resume();
        }
        m_size.fetch_sub(1, std::memory_order::release);
    }

    // Process until there are no ready tasks left.
    while (m_size.load(std::memory_order::acquire) > 0)
    {
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        // m_size will only drop to zero once all executing coroutines are finished
        // but the queue could be empty for threads that finished early.
        if (m_queue.empty())
        {
            std::cerr << "m_queue.empty() breaking final loop m_size = " << m_size.load(std::memory_order::acquire) << "\n";
            break;
        }

        auto handle = m_queue.front();
        m_queue.pop_front();
        lk.unlock();

        // Release the lock while executing the coroutine.
        if (handle == nullptr)
        {
            std::cerr << "handle is nullptr\n";
        }
        else if (handle.done())
        {
            std::cerr << "handle.done() == true\n";
        }
        else
        {
            std::cerr << "handle.resume()\n";
            handle.resume();
        }
        m_size.fetch_sub(1, std::memory_order::release);
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }

    std::cerr << "thread_pool::executor() return\n";
}

auto thread_pool::schedule_impl(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr || handle.done())
    {
        return;
    }

    {
        std::scoped_lock lk{m_wait_mutex};
        m_queue.emplace_back(handle);
        m_wait_cv.notify_one();
    }
}

} // namespace coro

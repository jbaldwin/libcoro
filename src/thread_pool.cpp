#include "coro/thread_pool.hpp"
#include "coro/detail/task_self_deleting.hpp"

#include <queue>

namespace coro
{
static constexpr std::size_t MAX_HANDLES{2};

thread_pool::schedule_operation::schedule_operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{

}

thread_pool::schedule_operation::schedule_operation(thread_pool& tp, bool force_global_queue) noexcept
    : m_thread_pool(tp),
      m_force_global_queue(force_global_queue)
{

}

auto thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_thread_pool.schedule_impl(awaiting_coroutine, m_force_global_queue);
}

thread_pool::thread_pool(options&& opts, private_constructor)
    : m_opts(opts)
{
    m_threads.reserve(m_opts.thread_count);
    m_executor_state.reserve(m_opts.thread_count);
    m_local_queues.reserve(m_opts.thread_count);
}

auto thread_pool::make_unique(options opts) -> std::unique_ptr<thread_pool>
{
    auto tp = std::make_unique<thread_pool>(std::move(opts), private_constructor{});

    // Initialize the background worker threads once the thread pool is fully constructed
    // so the workers have a full ready object to work with.
    for (uint32_t i = 0; i < tp->m_opts.thread_count; ++i)
    {
        tp->m_local_queues.emplace_back();
        tp->m_executor_state.emplace_back(std::make_unique<executor_state>());
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
    m_size.fetch_add(1, std::memory_order::release);
    if (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        return schedule_operation{*this};
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

    schedule_impl(handle, false);
    return true;
}

auto thread_pool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
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

auto thread_pool::try_steal_work(std::size_t my_idx, std::array<std::coroutine_handle<>, MAX_HANDLES>& handles) -> bool
{
    for (std::size_t i = 0; i < m_local_queues.size(); ++i)
    {
        if (i == my_idx)
        {
            continue;
        }

        auto& queue = m_local_queues[i];
        if (queue.try_dequeue_bulk(handles.data(), MAX_HANDLES))
        {
            return true;
        }
    }

    return false;
}

auto thread_pool::executor(std::size_t idx) -> void
{
    auto& state = *m_executor_state[idx].get();
    state.m_thread_id = std::this_thread::get_id();
    auto& local_queue = m_local_queues[idx];

    constexpr std::chrono::milliseconds wait_timeout{100};
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested.
    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        // Try and grab work from out local queue first.
        std::array<std::coroutine_handle<>, MAX_HANDLES> handles{nullptr};
        if (!local_queue.try_dequeue_bulk(handles.data(), MAX_HANDLES))
        {
            // Try and grab work from the global queue next.
            if (!m_global_queue.try_dequeue_bulk(handles.data(), MAX_HANDLES))
            {
                // Try and steal work from another local queue last.
                if (!try_steal_work(idx, handles))
                {
                    // If there is nothing on global and nothing to steal, try and lock to sleep
                    if (state.m_mutex.try_lock())
                    {
                        // Hold the lock for the duration of sleeping
                        std::scoped_lock lk{std::adopt_lock, state.m_mutex};
                        if (!m_global_queue.wait_dequeue_bulk_timed(handles.data(), MAX_HANDLES, wait_timeout))
                        {
                            // If we wait the full timeout and there is no work, probe around.
                            continue;
                        }
                    }
                    // else if we didn't get the lock we just enqueued on this thread (which should be impossible?)
                }
            }
        }

        for (std::size_t i = 0; i < MAX_HANDLES; ++i)
        {
            auto& handle = handles[i];
            if (handle == nullptr)
            {
                break;
            }

            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    // We'll lock our local thread so nothing new gets enqueued to it.
    std::scoped_lock lk{state.m_mutex};

    // Process until there are no ready tasks left, start by draining the local queue.
    while (true)
    {
        // Try and grab work from out local queue first.
        std::array<std::coroutine_handle<>, MAX_HANDLES> handles{nullptr};
        if (!local_queue.try_dequeue_bulk(handles.data(), MAX_HANDLES))
        {
            break;
        }

        for (std::size_t i = 0; i < MAX_HANDLES; ++i)
        {
            auto& handle = handles[i];
            if (handle == nullptr)
            {
                break;
            }

            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    // Now finish by draining the global queue.
    while (m_size.load(std::memory_order::acquire) > 0)
    {
        std::array<std::coroutine_handle<>, MAX_HANDLES> handles{nullptr};
        if (!m_global_queue.try_dequeue_bulk(handles.data(), MAX_HANDLES))
        {
            break;
        }

        for (std::size_t i = 0; i < MAX_HANDLES; ++i)
        {
            auto& handle = handles[i];
            if (handle == nullptr)
            {
                break;
            }

            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool::schedule_impl(std::coroutine_handle<> handle, bool force_global_queue) noexcept -> void
{
    if (handle == nullptr || handle.done())
    {
        return;
    }

    if (!force_global_queue)
    {
        // Attempt to see if we are on one of the thread_pool threads and enqueue to our local queue.
        for (std::size_t i = 0; i < m_executor_state.size(); i++)
        {
            // If we're on an executor thread and it is not sleeping enqueue locally, otherwise enqueue on the global queue to wake up a sleeping worker.
            auto& state = *m_executor_state[i].get();
            if (state.m_thread_id == std::this_thread::get_id())
            {
                // If we can lock and we're not sleeping enqueue locally.
                if (state.m_mutex.try_lock())
                {
                    std::scoped_lock lk{std::adopt_lock, state.m_mutex};
                    m_local_queues[i].enqueue(handle);
                    return;
                }

                // Either the lock couldn't be acquired without contention or the thread is sleeping
                // so enqueue globally.
                break;
            }
        }
    }

    m_global_queue.enqueue(handle);
}

} // namespace coro

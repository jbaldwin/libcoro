#include "coro/thread_pool.hpp"
#include "coro/detail/awaiter_list.hpp"
#include "coro/detail/task_self_deleting.hpp"

namespace coro
{
thread_pool::schedule_operation::schedule_operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{

}

auto thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    m_awaiting_coroutine = awaiting_coroutine;
    detail::awaiter_list_push(m_thread_pool.m_global_queue, this);

    // If there is at least one sleeping executor wake it up.
    if (m_thread_pool.m_sleeping_executors.load(std::memory_order::acquire) != 0)
    {
        std::scoped_lock lk{m_thread_pool.m_wait_mutex};
        m_thread_pool.m_wait_cv.notify_one();
    }
}

thread_pool::thread_pool(options&& opts, private_constructor) : m_opts(opts)
{
    m_threads.reserve(m_opts.thread_count);
    for (uint32_t i = 0; i < m_opts.thread_count; ++i)
    {
        m_executor_queues.emplace_back(nullptr);
    }
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

    auto* schedule_op = new schedule_operation(*this);
    schedule_op->m_allocated = true;
    schedule_op->await_suspend(handle);
    return true;
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
    auto executor_queues_it = m_executor_queues.begin();
    std::advance(executor_queues_it, idx);
    auto& local_queue = *executor_queues_it;

    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    // Process until shutdown is requested.
    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        std::unique_lock<std::mutex> lk{m_wait_mutex};
        m_sleeping_executors.fetch_add(1, std::memory_order::release);
        m_wait_cv.wait(lk, [&]() { return m_global_queue.load(std::memory_order::acquire) != nullptr || m_shutdown_requested.load(std::memory_order::acquire); });
        m_sleeping_executors.fetch_sub(1, std::memory_order::release);
        lk.unlock();

        auto* op = detail::awaiter_list_pop_all(m_global_queue);
        if (op == nullptr)
        {
            // Attempt to steal work.
            for (auto& executor_queue : m_executor_queues)
            {
                while ((op = detail::awaiter_list_pop(executor_queue)) != nullptr)
                {
                    op->m_awaiting_coroutine.resume();
                    m_size.fetch_sub(1, std::memory_order::release);
                    if (op->m_allocated)
                    {
                        delete op;
                    }
                }
            }
            continue;
        }

        // Move extra operations to local queue (they can still be stolen)
        op = detail::awaiter_list_reverse(op);
        if (op->m_next != nullptr)
        {
            detail::awaiter_list_store(local_queue, op->m_next);
        }

        // Process until local queue is empty.
        while (op != nullptr)
        {
            op->m_awaiting_coroutine.resume();
            m_size.fetch_sub(1, std::memory_order::release);
            if (op->m_allocated)
            {
                delete op;
            }

            op = detail::awaiter_list_pop(local_queue);
        }

        // Attempt to steal work.
        for (auto& executor_queue : m_executor_queues)
        {
            while ((op = detail::awaiter_list_pop(executor_queue)) != nullptr)
            {
                op->m_awaiting_coroutine.resume();
                m_size.fetch_sub(1, std::memory_order::release);
                if (op->m_allocated)
                {
                    delete op;
                }
            }
        }
    }

    // Process until there are no ready tasks left.
    while (m_size.load(std::memory_order::acquire) > 0)
    {
        auto* op = detail::awaiter_list_pop_all(m_global_queue);
        if (op == nullptr)
        {
            // Attempt to steal work on shutdown since the global queue is empty.
            for (auto& executor_queue : m_executor_queues)
            {
                while ((op = detail::awaiter_list_pop(executor_queue)) != nullptr)
                {
                    op->m_awaiting_coroutine.resume();
                    m_size.fetch_sub(1, std::memory_order::release);
                    if (op->m_allocated)
                    {
                        delete op;
                    }
                }
            }

            break;
        }

        // Move operations to local queue (they can still be stolen)
        op = detail::awaiter_list_reverse(op);
        if (op->m_next != nullptr)
        {
            detail::awaiter_list_store(local_queue, op->m_next);
        }

        // Process until local queue is empty.
        while (op != nullptr)
        {
            op->m_awaiting_coroutine.resume();
            m_size.fetch_sub(1, std::memory_order::release);
            if (op->m_allocated)
            {
                delete op;
            }

            op = detail::awaiter_list_pop(local_queue);
        }

        // Attempt to steal work.
        for (auto& executor_queue : m_executor_queues)
        {
            while ((op = detail::awaiter_list_pop(executor_queue)) != nullptr)
            {
                op->m_awaiting_coroutine.resume();
                m_size.fetch_sub(1, std::memory_order::release);
                if (op->m_allocated)
                {
                    delete op;
                }
            }
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

} // namespace coro

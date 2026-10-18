#include "coro/thread_pool.hpp"
#include "coro/detail/task_self_deleting.hpp"

#include <cstdint>

namespace coro
{

namespace detail
{
static auto
    make_spawned_joinable_wait_task(std::unique_ptr<coro::task_group<coro::thread_pool>> group_ptr) -> coro::task<void>
{
    co_await *group_ptr;
    co_return;
}

} // namespace detail

namespace
{
/// Identifies the executor thread running on this thread so `schedule()` can find its queue.
struct current_executor
{
    const thread_pool* m_thread_pool{nullptr};
    std::size_t        m_idx{0};
};

thread_local current_executor t_current_executor{};

/// Executors check the shared queue first once every this many iterations, so tasks resumed or scheduled from other
/// threads are not starved by long running fork-join work on the executor's own queue.
constexpr std::uint32_t global_queue_interval{61};
} // namespace

thread_pool::schedule_operation::schedule_operation(thread_pool& tp) noexcept : m_thread_pool(tp)
{
}

auto thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) -> void
{
    if (!m_thread_pool.enqueue(awaiting_coroutine, m_yield))
    {
        throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
    }
}

thread_pool::thread_pool(options&& opts, private_constructor)
    : m_opts(opts),
      m_worker_queues(std::make_unique<worker_queue[]>(m_opts.thread_count))
{ m_threads.reserve(m_opts.thread_count); }

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
    if (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        return schedule_operation{*this};
    }
    else
    {
        throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
    }
}

auto thread_pool::yield() -> schedule_operation
{
    auto operation    = schedule();
    operation.m_yield = true;
    return operation;
}

auto thread_pool::spawn_detached(coro::task<void>&& task) noexcept -> bool
{
    auto wrapper_task = detail::make_task_self_deleting(std::move(task));
    auto handle       = wrapper_task.handle();
    if (!resume(handle))
    {
        handle.destroy();
        return false;
    }
    return true;
}

auto thread_pool::spawn_joinable(coro::task<void>&& task) noexcept -> coro::task<void>
{
    auto group_ptr = std::make_unique<coro::task_group<coro::thread_pool>>(this, std::move(task));
    return detail::make_spawned_joinable_wait_task(std::move(group_ptr));
}

auto thread_pool::resume(std::coroutine_handle<> handle) noexcept -> bool
{
    if (handle == nullptr || handle.done())
    {
        return false;
    }

    return enqueue(handle, true);
}

auto thread_pool::shutdown() noexcept -> void
{
    {
        std::scoped_lock lk{m_wait_mutex};
        if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel))
        {
            return;
        }
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

auto thread_pool::executor(std::size_t idx) -> void
{
    t_current_executor = current_executor{.m_thread_pool = this, .m_idx = idx};

    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    std::uint32_t iterations{0};
    while (true)
    {
        std::coroutine_handle<> handle{};
        if (++iterations == global_queue_interval)
        {
            iterations = 0;
            handle     = pop_global();
        }
        if (handle == nullptr)
        {
            handle = pop_local(idx);
        }
        if (handle == nullptr)
        {
            handle = pop_global();
        }
        if (handle == nullptr)
        {
            handle = steal(idx);
        }

        if (handle == nullptr)
        {
            std::unique_lock lk{m_wait_mutex};
            m_sleeping.fetch_add(1, std::memory_order::relaxed);

            // Register before rechecking every queue. A producer either publishes before its queue is checked,
            // or acquires that queue's mutex afterwards and observes this registration before taking m_wait_mutex.
            // Keep the registration on exit so empty() also accounts for stopped executors.
            const auto stopping = m_shutdown_requested.load(std::memory_order::acquire);
            handle = pop_local(idx);
            if (handle == nullptr)
            {
                handle = pop_global();
            }
            if (handle == nullptr)
            {
                handle = steal(idx);
            }
            if (handle == nullptr)
            {
                if (stopping)
                {
                    break;
                }
                m_wait_cv.wait(lk);
            }
            m_sleeping.fetch_sub(1, std::memory_order::relaxed);
        }

        if (handle != nullptr)
        {
            handle.resume();
        }
    }

    t_current_executor = current_executor{};

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto thread_pool::enqueue(std::coroutine_handle<> handle, bool global) noexcept -> bool
{
    if (!global && t_current_executor.m_thread_pool == this)
    {
        auto&            local = m_worker_queues[t_current_executor.m_idx];
        std::scoped_lock lk{local.m_mutex};
        if (m_shutdown_requested.load(std::memory_order::acquire))
        {
            return false;
        }
        local.m_queue.emplace_back(handle);
    }
    else
    {
        std::scoped_lock lk{m_global_mutex};
        if (m_shutdown_requested.load(std::memory_order::acquire))
        {
            return false;
        }
        m_global_queue.emplace_back(handle);
    }

    wake(1);
    return true;
}

auto thread_pool::pop_local(std::size_t idx) noexcept -> std::coroutine_handle<>
{
    auto&            local = m_worker_queues[idx];
    std::scoped_lock lk{local.m_mutex};
    if (local.m_queue.empty())
    {
        return nullptr;
    }

    auto handle = local.m_queue.back();
    local.m_queue.pop_back();
    return handle;
}

auto thread_pool::pop_global() noexcept -> std::coroutine_handle<>
{
    std::scoped_lock lk{m_global_mutex};
    if (m_global_queue.empty())
    {
        return nullptr;
    }

    auto handle = m_global_queue.front();
    m_global_queue.pop_front();
    return handle;
}

auto thread_pool::steal(std::size_t idx) noexcept -> std::coroutine_handle<>
{
    // Take a single task and go back to the own queue: draining a victim would expand its pending tasks breadth
    // first and keep all of their coroutine frames alive at once.
    for (std::size_t i = 1; i < m_opts.thread_count; ++i)
    {
        auto&            victim = m_worker_queues[(idx + i) % m_opts.thread_count];
        std::scoped_lock lk{victim.m_mutex};
        if (!victim.m_queue.empty())
        {
            auto handle = victim.m_queue.front();
            victim.m_queue.pop_front();
            return handle;
        }
    }

    return nullptr;
}

auto thread_pool::wake(std::size_t count) noexcept -> void
{
    if (count == 0 || m_sleeping.load(std::memory_order::relaxed) == 0)
    {
        return;
    }

    std::scoped_lock lk{m_wait_mutex};
    if (count >= m_opts.thread_count)
    {
        m_wait_cv.notify_all();
    }
    else
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            m_wait_cv.notify_one();
        }
    }
}

auto thread_pool::empty() const noexcept -> bool
{
    std::scoped_lock lk{m_wait_mutex};
    if (m_sleeping.load(std::memory_order::relaxed) != m_threads.size())
    {
        return false;
    }
    std::scoped_lock global_lk{m_global_mutex};
    return m_global_queue.empty();
}

auto thread_pool::queue_empty() const noexcept -> bool
{
    std::scoped_lock global_lk{m_global_mutex};
    if (!m_global_queue.empty())
    {
        return false;
    }
    for (std::size_t idx = 0; idx < m_opts.thread_count; ++idx)
    {
        auto&            local = m_worker_queues[idx];
        std::scoped_lock lk{local.m_mutex};
        if (!local.m_queue.empty())
        {
            return false;
        }
    }
    return true;
}

} // namespace coro

#include "coro/async_thread_pool.hpp"
#include "coro/sync_wait.hpp"
#include "coro/event.hpp"

namespace coro
{
async_thread_pool::async_thread_pool(options&& opts, private_constructor)
    : m_opts(opts)
{
    m_threads.reserve(m_opts.thread_count);
}

async_thread_pool::~async_thread_pool()
{
    shutdown();
}

auto async_thread_pool::make_shared(options opts) -> std::shared_ptr<async_thread_pool>
{
    auto atp = std::make_shared<async_thread_pool>(std::move(opts), private_constructor{});

    for (uint32_t i = 0; i < atp->m_opts.thread_count; ++i)
    {
        atp->m_threads.emplace_back([atp, i]() { atp->executor(i); });
    }

    return atp;
}

auto async_thread_pool::schedule() -> coro::task<void>
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        throw std::runtime_error("coro::async_thread_pool is shutting down, unable to schedule new tasks.");
    }

    coro::event e{};
    auto schedule_task = make_schedule_task(e);

    m_size.fetch_add(1, std::memory_order::release);
    if (co_await m_queue.produce(std::move(schedule_task)) != coro::ring_buffer_result::produce::produced)
    {
        co_await e;
        co_return;
    }

    m_size.fetch_sub(1, std::memory_order::release);
    throw std::runtime_error("coro::async_thread_pool is shutting down, unable to schedule new tasks.");
}

auto async_thread_pool::spawn(coro::task<void> task) -> bool
{
    if (m_shutdown_requested.load(std::memory_order::acquire))
    {
        return false;
    }

    // Increment 2, the spawn schedule task and the user task.
    m_size.fetch_add(2, std::memory_order::release);
    auto schedule_task = make_spawn_schedule_task(std::move(task));
    schedule_task.promise().executor_size(m_size);
    schedule_task.resume();
    return true;
}

auto async_thread_pool::resume(std::coroutine_handle<> handle) -> bool
{
    return spawn(make_resume_task(handle));
}

auto async_thread_pool::shutdown() -> void
{

    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        auto this_shared = shared_from_this();
        coro::sync_wait(m_queue.shutdown_drain<async_thread_pool>(std::move(this_shared)));

        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
}

auto async_thread_pool::executor(std::size_t idx) -> void
{
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    while (!m_shutdown_requested.load(std::memory_order::acquire))
    {
        auto result = coro::sync_wait(m_queue.consume());
        if (result.has_value())
        {
            result.value().resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    while (!m_queue.empty())
    {
        auto result = coro::sync_wait(m_queue.consume());
        if (result.has_value())
        {
            result.value().resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

} // namespace coro

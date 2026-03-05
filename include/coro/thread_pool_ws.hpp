#pragma once

#include "coro/detail/awaiter_list.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <riften/deque.hpp>
#include <thread>

namespace coro
{

class thread_pool_ws
{
public:
    struct options
    {
        uint32_t worker_count = std::thread::hardware_concurrency();
    };

    class schedule_operation
    {
        friend class thread_pool_ws;
        explicit schedule_operation(thread_pool_ws& tp) noexcept : m_thread_pool(tp) {}

    public:
        auto await_ready() noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
        {
            m_awaiting_coroutine = awaiting_coroutine;
            // See if we are running on a thread pool worker to enqueue locally.
            auto& idx = thread_pool_ws::m_thread_pool_queue_idx;
            if (idx.has_value())
            {
                m_thread_pool.m_queues[idx.value()]->emplace(this);
            }
            else
            {
                detail::awaiter_list_push(m_thread_pool.m_global_queue, this);
            }
        }
        auto await_resume() noexcept -> void {}

        schedule_operation*     m_next{nullptr};
        std::coroutine_handle<> m_awaiting_coroutine{nullptr};

    private:
        thread_pool_ws& m_thread_pool;
    };

    thread_pool_ws(options opts = options{.worker_count = std::thread::hardware_concurrency()})
    {
        for (uint32_t i = 0; i < opts.worker_count; ++i)
        {
            m_queues.emplace_back(std::make_unique<riften::Deque<schedule_operation*>>());
        }

        m_workers.reserve(opts.worker_count);
        for (uint32_t i = 0; i < opts.worker_count; ++i)
        {
            m_workers.emplace_back([this, i](std::stop_token st) -> void { this->execute(std::move(st), i); });
        }
    }

    ~thread_pool_ws();

    thread_pool_ws(const thread_pool_ws&)                             = delete;
    thread_pool_ws(thread_pool_ws&&)                                  = delete;
    auto operator=(const thread_pool_ws&) noexcept -> thread_pool_ws& = delete;
    auto operator=(thread_pool_ws&&) noexcept -> thread_pool_ws&      = delete;

    auto schedule() -> schedule_operation;
    template<typename return_type>
    auto schedule(coro::task<return_type> user_task) -> coro::task<return_type>
    {
        co_await schedule();
        co_return co_await user_task;
    }

    auto spawn_detached(coro::task<void> user_task) -> bool
    {
        (void)user_task;
        return false;
    }

    auto spawn_joinable(coro::task<void> user_task) -> coro::task<void> { co_return co_await user_task; }

    auto yield() -> schedule_operation { return schedule_operation{*this}; }

    auto resume(std::coroutine_handle<> handle) -> bool
    {
        (void)handle;
        return true;
    }

    auto size() -> std::size_t { return 0; }

    auto empty() -> bool { return false; }

    auto shutdown() -> void {}

private:
    friend schedule_operation;

    std::vector<std::jthread>                                        m_workers{};
    std::vector<std::unique_ptr<riften::Deque<schedule_operation*>>> m_queues;
    std::atomic<schedule_operation*>                                 m_global_queue{nullptr};
    static thread_local std::optional<uint32_t>                      m_thread_pool_queue_idx;

    auto execute(std::stop_token st, uint32_t idx) -> void;
    auto drain_thread_queue(riften::Deque<schedule_operation*>& queue) -> bool;
    auto try_steal(uint32_t my_idx) -> bool;
    auto drain_peer_queue(riften::Deque<schedule_operation*>& queue) -> bool;
    auto drain_global_queue(riften::Deque<schedule_operation*>& queue) -> bool;
};

} // namespace coro

#pragma once

#include "coro/shutdown.hpp"

#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <optional>

#include <iostream>

namespace coro
{

class thread_pool;

class thread_pool
{
public:
    class operation
    {
        friend class thread_pool;
    public:
        explicit operation(thread_pool& tp) noexcept;

        auto await_ready() noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void;
        auto await_resume() noexcept -> void { /* no-op */ }
    private:
        thread_pool& m_thread_pool;
        std::coroutine_handle<> m_awaiting_coroutine{nullptr};
    };

    explicit thread_pool(uint32_t thread_count = std::thread::hardware_concurrency());

    thread_pool(const thread_pool&) = delete;
    thread_pool(thread_pool&&) = delete;
    auto operator=(const thread_pool&) -> thread_pool& = delete;
    auto operator=(thread_pool&&) -> thread_pool& = delete;

    ~thread_pool();

    auto thread_count() const -> uint32_t { return m_threads.size(); }

    [[nodiscard]]
    auto schedule() noexcept -> std::optional<operation>;

    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) -> void;

    auto size() const -> std::size_t { return m_size.load(std::memory_order::relaxed); }
    auto empty() const -> bool { return size() == 0; }
private:
    std::atomic<bool> m_shutdown_requested{false};

    std::vector<std::thread> m_threads;

    std::mutex m_queue_cv_mutex;
    std::condition_variable m_queue_cv;

    std::mutex m_queue_mutex;
    std::deque<operation*> m_queue;
    std::atomic<std::size_t> m_size{0};

    auto run(uint32_t worker_idx) -> void;
    auto join() -> void;
    auto schedule_impl(operation* op) -> void;
};

} // namespace coro

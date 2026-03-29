#pragma once

#include "coro/detail/awaiter_list.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <riften/deque.hpp>
#include <thread>

namespace coro
{

class thread_pool_ws
{
    struct private_constructor
    {
        explicit private_constructor() = default;
    };

public:
    class schedule_operation;

    struct worker_info
    {
        worker_info(thread_pool_ws& tp, uint32_t i);
        ~worker_info() = default;

        worker_info(const worker_info&)                             = delete;
        worker_info(worker_info&&)                                  = delete;
        auto operator=(const worker_info&) noexcept -> worker_info& = delete;
        auto operator=(worker_info&&) noexcept -> worker_info&      = delete;

        std::jthread                       m_thread;
        std::mutex                         m_wait_mutex{};
        std::condition_variable_any        m_wait_cv{};
        riften::Deque<schedule_operation*> m_queue{};
        std::atomic<bool>                  m_asleep{false};
    };

    struct options
    {
        /// @brief The number of executor threads for this thread pool. Uses std::hardware_concurrency() by default.
        uint32_t thread_count = std::thread::hardware_concurrency();
        /// @brief Functor to call on each executor thread upon starting execution. The parameter is the thread's ID
        /// assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_start_functor = nullptr;
        /// @brief Functor to call on each executor thread upon stopping execution. The parameter is the thread's ID
        /// assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_stop_functor = nullptr;
    };

    class schedule_operation
    {
        friend class thread_pool_ws;
        explicit schedule_operation(thread_pool_ws& tp) noexcept;

    public:
        auto await_ready() noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void;
        auto await_resume() noexcept -> void {}

        schedule_operation*     m_next{nullptr};
        std::coroutine_handle<> m_awaiting_coroutine{nullptr};

    private:
        thread_pool_ws& m_thread_pool;
        bool            m_allocated{false};
    };

    /**
     * @see thread_pool_ws::make_unique
     */
    thread_pool_ws(options opts, private_constructor);

    static auto make_unique(
        options opts = options{
            .thread_count            = std::thread::hardware_concurrency(),
            .on_thread_start_functor = nullptr,
            .on_thread_stop_functor  = nullptr,
        }) -> std::unique_ptr<thread_pool_ws>;

    ~thread_pool_ws();

    thread_pool_ws(const thread_pool_ws&)                             = delete;
    thread_pool_ws(thread_pool_ws&&)                                  = delete;
    auto operator=(const thread_pool_ws&) noexcept -> thread_pool_ws& = delete;
    auto operator=(thread_pool_ws&&) noexcept -> thread_pool_ws&      = delete;

    [[nodiscard]] auto thread_count() const noexcept -> size_t { return m_workers.size(); }

    [[nodiscard]] auto schedule() -> schedule_operation;

    template<typename return_type>
    [[nodiscard]] auto schedule(coro::task<return_type> user_task) -> coro::task<return_type>
    {
        co_await schedule();
        co_return co_await user_task;
    }
    auto spawn_detached(coro::task<void> user_task) -> bool;
    auto spawn_joinable(coro::task<void> user_task) -> coro::task<void>;

    [[nodiscard]] auto yield() -> schedule_operation { return schedule_operation{*this}; }
    auto               resume(std::coroutine_handle<> handle) noexcept -> bool;
    auto               size() noexcept -> std::size_t { return m_size.load(std::memory_order::acquire); }
    auto               empty() noexcept -> bool { return size() == 0; }
    [[nodiscard]] auto is_shutdown() const -> bool { return m_shutdown_requested.load(std::memory_order::acquire); }
    auto               shutdown() noexcept -> void;

private:
    friend schedule_operation;

    options                                     m_opts;
    std::atomic<bool>                           m_shutdown_requested{false};
    std::atomic<uint64_t>                       m_size{0};
    std::vector<std::unique_ptr<worker_info>>   m_workers{};
    std::atomic<schedule_operation*>            m_global_queue{nullptr};
    std::atomic<uint32_t>                       m_sleeping{0};
    static thread_local std::optional<uint32_t> m_thread_pool_queue_idx;

    auto execute(std::stop_token st, uint32_t idx) -> void;
    auto drain_thread_queue(riften::Deque<schedule_operation*>& queue) -> bool;
    auto try_steal(uint32_t my_idx) -> bool;
    auto drain_peer_queue(riften::Deque<schedule_operation*>& queue) -> bool;
    auto drain_global_queue(riften::Deque<schedule_operation*>& queue) -> bool;
    auto resume_task(std::optional<schedule_operation*> op) -> bool;
    auto try_wake_worker() noexcept -> void;
};

} // namespace coro

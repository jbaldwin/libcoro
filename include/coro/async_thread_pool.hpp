#pragma once

#include "coro/detail/task_self_deleting.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/event.hpp"
#include "coro/task.hpp"
#include "coro/mutex.hpp"
#include "coro/ring_buffer.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace coro
{

class async_thread_pool : public std::enable_shared_from_this<async_thread_pool>
{
    struct private_constructor
    {
        private_constructor() = default;
    };

public:
    struct options
    {
        /// The number of executor threads for this thread pool. Uses the hardware concurrency
        /// value by default.
        uint32_t thread_count = std::thread::hardware_concurrency();
        /// Functor to call on each executor thread upon starting execution. The parameter is the
        /// thread's ID assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_start_functor = nullptr;
        /// Functor to call on each executor thread upon stopping execution. The parameter is the
        /// thread's ID assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_stop_functor = nullptr;
    };

    async_thread_pool(options&& opts, private_constructor);
    virtual ~async_thread_pool();

    /**
     * @brief Creates a thread pool executor.
     *
     * @param opts The thread pool's options.
     * @return std::shared_ptr<thread_pool>
     */
    static auto make_shared(
        options opts = options{
            .thread_count            = std::thread::hardware_concurrency(),
            .on_thread_start_functor = nullptr,
            .on_thread_stop_functor  = nullptr}) -> std::shared_ptr<async_thread_pool>;

    async_thread_pool(const async_thread_pool&) = delete;
    async_thread_pool(async_thread_pool&&) = delete;
    auto operator=(const async_thread_pool&) -> async_thread_pool& = delete;
    auto operator=(async_thread_pool&&) -> async_thread_pool& = delete;

    [[nodiscard]] auto schedule() -> coro::task<void>;
    template<typename return_type>
    [[nodiscard]] auto schedule(coro::task<return_type> task) -> coro::task<return_type>
    {
        co_await schedule();
        co_return co_await task;
    }
    auto spawn(coro::task<void> task) -> bool;
    [[nodiscard]] auto yield() -> coro::task<void> { return schedule(); }
    auto resume(std::coroutine_handle<> handle) -> bool;
    auto size() -> std::size_t { return m_size.load(std::memory_order::acquire); }
    auto empty() -> bool { return size() == 0; }
    auto shutdown() -> void;

private:
    options m_opts;
    std::atomic<std::size_t> m_size{0};
    coro::ring_buffer<coro::task<void>, 64> m_queue{};
    std::vector<std::thread> m_threads{};

    std::atomic<bool> m_shutdown_requested{false};

    auto make_schedule_task(coro::event& e) -> coro::task<void>
    {
        e.set();
        co_return;
    }

    auto make_spawn_schedule_task(coro::task<void>&& task) -> coro::detail::task_self_deleting
    {
        co_await m_queue.produce(std::forward<coro::task<void>>(task));
        co_return; // result doesn't matter, this was spawned.
    }

    auto make_resume_task(std::coroutine_handle<> handle) -> coro::task<void>
    {
        handle.resume();
        co_return;
    }

    auto executor(std::size_t idx) -> void;
};


} // namespace coro

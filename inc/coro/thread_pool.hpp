#pragma once

#include "coro/shutdown.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <optional>
#include <functional>

namespace coro
{

/**
 * Creates a thread pool that executes arbitrary coroutine tasks in a FIFO scheduler policy.
 * The thread pool by default will create an execution thread per available core on the system.
 *
 * When shutting down, either by the thread pool destructing or by manually calling shutdown()
 * the thread pool will stop accepting new tasks but will complete all tasks that were scheduled
 * prior to the shutdown request.
 */
class thread_pool
{
public:
    /**
     * An operation is an awaitable type with a coroutine to resume the task scheduled on one of
     * the executor threads.
     */
    class operation
    {
        friend class thread_pool;
        /**
         * Only thread_pool's can create operations when a task is being scheduled.
         * @param tp The thread pool that created this operation.
         */
        explicit operation(thread_pool& tp) noexcept;
    public:
        /**
         * Operations always pause so the executing thread and be switched.
         */
        auto await_ready() noexcept -> bool { return false; }

        /**
         * Suspending always returns to the caller (using void return of await_suspend()) and
         * stores the coroutine internally for the executing thread to resume from.
         */
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void;

        /**
         * no-op as this is the function called first by the thread pool's executing thread.
         */
        auto await_resume() noexcept -> void { }
    private:
        /// The thread pool that this operation will execute on.
        thread_pool& m_thread_pool;
        /// The coroutine awaiting execution.
        std::coroutine_handle<> m_awaiting_coroutine{nullptr};
    };

    struct options
    {
        /// The number of executor threads for this thread pool.  Uses the hardware concurrency
        /// value by default.
        uint32_t thread_count = std::thread::hardware_concurrency();
        /// Functor to call on each executor thread upon starting execution.
        std::function<void(std::size_t)> on_thread_start_functor = nullptr;
        /// Functor to call on each executor thread upon stopping execution.
        std::function<void(std::size_t)> on_thread_stop_functor = nullptr;
    };

    /**
     * @param opts Thread pool configuration options.
     */
    explicit thread_pool(options opts = options{
        std::thread::hardware_concurrency(),
        nullptr,
        nullptr
    });

    thread_pool(const thread_pool&) = delete;
    thread_pool(thread_pool&&) = delete;
    auto operator=(const thread_pool&) -> thread_pool& = delete;
    auto operator=(thread_pool&&) -> thread_pool& = delete;

    ~thread_pool();

    auto thread_count() const noexcept -> uint32_t { return m_threads.size(); }

    /**
     * Schedules the currently executing coroutine to be run on this thread pool.  This must be
     * called from within the coroutines function body to schedule the coroutine on the thread pool.
     * @return The operation to switch from the calling scheduling thread to the executor thread
     *         pool thread.  This will return nullopt if the schedule fails, currently the only
     *         way for this to fail is if `shudown()` has been called.
     */
    [[nodiscard]]
    auto schedule() noexcept -> std::optional<operation>;

    /**
     * @throw std::runtime_error If the thread pool is `shutdown()` scheduling new tasks is not permitted.
     * @param f The function to execute on the thread pool.
     * @param args The arguments to call the functor with.
     * @return A task that wraps the given functor to be executed on the thread pool.
     */
    template<typename functor, typename... arguments>
    [[nodiscard]]
    auto schedule(functor&& f, arguments... args) noexcept -> task<decltype(f(std::forward<arguments>(args)...))>
    {
        auto scheduled = schedule();
        if(!scheduled.has_value())
        {
            throw std::runtime_error("coro::thread_pool is shutting down, unable to schedule new tasks.");
        }

        co_await scheduled.value();

        if constexpr (std::is_same_v<void, decltype(f(std::forward<arguments>(args)...))>)
        {
            f(std::forward<arguments>(args)...);
            co_return;
        }
        else
        {
            co_return f(std::forward<arguments>(args)...);
        }
    }

    /**
     * Shutsdown the thread pool.  This will finish any tasks scheduled prior to calling this
     * function but will prevent the thread pool from scheduling any new tasks.
     * @param wait_for_tasks Should this function block until all remaining scheduled tasks have
     *                       completed?  Pass in sync to wait, or async to not block.
     */
    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) noexcept -> void;

    /**
     * @return The number of tasks waiting in the task queue + the executing tasks.
     */
    auto size() const noexcept -> std::size_t { return m_size.load(std::memory_order::relaxed); }

    /**
     * @return True if the task queue is empty and zero tasks are currently executing.
     */
    auto empty() const noexcept -> bool { return size() == 0; }

    /**
     * @return The number of tasks waiting in the task queue to be executed.
     */
    auto queue_size() const noexcept -> std::size_t
    {
        // Might not be totally perfect but good enough, avoids acquiring the lock for now.
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_queue.size();
    }

    /**
     * @return True if the task queue is currently empty.
     */
    auto queue_empty() const noexcept -> bool { return queue_size() == 0; }
private:
    /// The configuration options.
    options m_opts;

    /// Has the thread pool been requested to shut down?
    std::atomic<bool> m_shutdown_requested{false};

    /// The background executor threads.
    std::vector<std::jthread> m_threads;

    /// Mutex for executor threads to sleep on the condition variable.
    std::mutex m_wait_mutex;
    /// Condition variable for each executor thread to wait on when no tasks are available.
    std::condition_variable_any m_wait_cv;

    /// Mutex to guard the queue of FIFO tasks.
    std::mutex m_queue_mutex;
    /// FIFO queue of tasks waiting to be executed.
    std::deque<operation*> m_queue;

    /// The number of tasks in the queue + currently executing.
    std::atomic<std::size_t> m_size{0};

    /**
     * Each background thread runs from this function.
     * @param stop_token Token which signals when shutdown() has been called.
     * @param idx The executor's idx for internal data structure accesses.
     */
    auto executor(std::stop_token stop_token, std::size_t idx) -> void;

    /**
     * @param op Schedules the given operation to be executed upon the first available thread.
     */
    auto schedule(operation* op) noexcept -> void;
};

} // namespace coro

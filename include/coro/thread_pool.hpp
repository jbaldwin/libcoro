#pragma once

#include "coro/detail/vendor/cameron314/concurrentqueue/blockingconcurrentqueue.h"
#include "coro/concepts/range_of.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <variant>
#include <vector>

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
    struct private_constructor
    {
        explicit private_constructor() = default;
    };
public:
    /**
     * A schedule operation is an awaitable type with a coroutine to resume the task scheduled on one of
     * the executor threads.
     */
    class schedule_operation
    {
        friend class thread_pool;
        /**
         * Only thread_pools can create schedule operations when a task is being scheduled.
         * @param tp The thread pool that created this schedule operation.
         */
        explicit schedule_operation(thread_pool& tp) noexcept;

    public:
        explicit schedule_operation(thread_pool& tp, bool force_global_queue) noexcept;

        /**
         * Schedule operations always pause so the executing thread can be switched.
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
        auto await_resume() noexcept -> void {}

    private:
        /// @brief The thread pool that this schedule operation will execute on.
        thread_pool& m_thread_pool;
        bool m_force_global_queue{false};
    };

    struct options
    {
        /// The number of executor threads for this thread pool.  Uses the hardware concurrency
        /// value by default.
        uint32_t thread_count = std::thread::hardware_concurrency();
        /// Functor to call on each executor thread upon starting execution.  The parameter is the
        /// thread's ID assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_start_functor = nullptr;
        /// Functor to call on each executor thread upon stopping execution.  The parameter is the
        /// thread's ID assigned to it by the thread pool.
        std::function<void(std::size_t)> on_thread_stop_functor = nullptr;
    };

    /**
     * @see thread_pool::make_unique
     */
    explicit thread_pool(options&& opts, private_constructor);

    /**
     * @brief Creates a thread pool executor.
     *
     * @param opts The thread pool's options.
     * @return std::unique_ptr<thread_pool>
     */
    static auto make_unique(
        options opts = options{
            .thread_count            = std::thread::hardware_concurrency(),
            .on_thread_start_functor = nullptr,
            .on_thread_stop_functor  = nullptr}) -> std::unique_ptr<thread_pool>;

    thread_pool(const thread_pool&)                    = delete;
    thread_pool(thread_pool&&)                         = delete;
    auto operator=(const thread_pool&) -> thread_pool& = delete;
    auto operator=(thread_pool&&) -> thread_pool&      = delete;

    virtual ~thread_pool();

    /**
     * @return The number of executor threads for processing tasks.
     */
    auto thread_count() const noexcept -> size_t { return m_threads.size(); }

    /**
     * Schedules the currently executing coroutine to be run on this thread pool.  This must be
     * called from within the coroutines function body to schedule the coroutine on the thread pool.
     * @throw std::runtime_error If the thread pool is `shutdown()` scheduling new tasks is not permitted.
     * @return The schedule operation to switch from the calling scheduling thread to the executor thread
     *         pool thread.
     */
    [[nodiscard]] auto schedule() -> schedule_operation;

    /**
     * Spawns the given task to be run on this thread pool, the task is detached from the user.
     * @param task The task to spawn onto the thread pool.
     * @return True if the task has been spawned onto this thread pool.
     */
    auto spawn(coro::task<void>&& task) noexcept -> bool;

    /**
     * Schedules a task on the thread pool and returns another task that must be awaited on for completion.
     * This can be done via co_await in a coroutine context or coro::sync_wait() outside of coroutine context.
     * @tparam return_type The return value of the task.
     * @param task The task to schedule on the thread pool.
     * @return The task to await for the input task to complete.
     */
    template<typename return_type>
    [[nodiscard]] auto schedule(coro::task<return_type> task) -> coro::task<return_type>
    {
        co_await schedule();
        co_return co_await task;
    }

    /**
     * Schedules any coroutine handle that is ready to be resumed.
     * @param handle The coroutine handle to schedule.
     * @return True if the coroutine is resumed, false if its a nullptr or the coroutine is already done.
     */
    auto resume(std::coroutine_handle<> handle) noexcept -> bool;

    /**
     * Schedules the set of coroutine handles that are ready to be resumed.
     * @param handles The coroutine handles to schedule.
     * @param uint64_t The number of tasks resumed, if any are null they are discarded.
     */
    template<coro::concepts::range_of<std::coroutine_handle<>> range_type>
    auto resume(const range_type& handles) noexcept -> uint64_t
    {
        m_size.fetch_add(std::size(handles), std::memory_order::release);

        size_t null_handles{0};

        for (const auto& handle : handles)
        {
            if (handle != nullptr) [[likely]]
            {
                m_global_queue.enqueue(handle);
            }
            else
            {
                ++null_handles;
            }
        }

        if (null_handles > 0)
        {
            m_size.fetch_sub(null_handles, std::memory_order::release);
        }

        return std::size(handles) - null_handles;
    }

    /**
     * Immediately yields the current task and places it at the end of the queue of tasks waiting
     * to be processed.  This will immediately be picked up again once it naturally goes through the
     * FIFO task queue.  This function is useful to yielding long processing tasks to let other tasks
     * get processing time.
     */
    [[nodiscard]] auto yield() -> schedule_operation { return schedule_operation{*this, true}; }

    /**
     * Shuts down the thread pool.  This will finish any tasks scheduled prior to calling this
     * function but will prevent the thread pool from scheduling any new tasks.  This call is
     * blocking and will wait until all inflight tasks are completed before returning.
     */
    auto shutdown() noexcept -> void;

    [[nodiscard]] auto is_shutdown() const -> bool { return m_shutdown_requested.load(std::memory_order::acquire); }

    /**
     * @return The number of tasks waiting in the task queue + the executing tasks.
     */
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_size.load(std::memory_order::acquire); }

    /**
     * @return True if the task queue is empty and zero tasks are currently executing.
     */
    [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }

    /**
     * @return The approximate number of tasks waiting in the task queue to be executed.
     */
    [[nodiscard]] auto queue_size() const noexcept -> std::size_t
    {
        std::size_t approx_size{0};
        for (auto& queue : m_local_queues)
        {
            approx_size += queue.size_approx();
        }
        return approx_size + m_global_queue.size_approx();
    }

    /**
     * @return True if the task queue is currently empty.
     */
    [[nodiscard]] auto queue_empty() const noexcept -> bool { return queue_size() == 0; }

private:
    struct executor_state
    {
        std::thread::id m_thread_id;
        std::mutex m_mutex;
    };

    /// The configuration options.
    options m_opts;
    /// The background executor threads.
    std::vector<std::thread> m_threads;
    /// Local executor worker thread queues.
    std::vector<moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>> m_local_queues;
    /// Global queue.
    moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>> m_global_queue;

    std::vector<std::unique_ptr<executor_state>> m_executor_state;

    auto try_steal_work(std::size_t my_idx, std::array<std::coroutine_handle<>, 4>& handles) -> bool;

    /**
     * Each background thread runs from this function.
     * @param idx The executor's idx for internal data structure accesses.
     */
    auto executor(std::size_t idx) -> void;
    /**
     * @param handle Schedules the given coroutine to be executed upon the first available thread.
     */
    auto schedule_impl(std::coroutine_handle<> handle, bool force_global_queue) noexcept -> void;

    /// The number of tasks in the queue + currently executing.
    std::atomic<std::size_t> m_size{0};
    /// Has the thread pool been requested to shut down?
    std::atomic<bool> m_shutdown_requested{false};
};

} // namespace coro

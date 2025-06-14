#pragma once

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
class thread_pool : public std::enable_shared_from_this<thread_pool>
{
    struct private_constructor
    {
        private_constructor() = default;
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
     * @see thread_pool::make_shared
     */
    explicit thread_pool(options&& opts, private_constructor);

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
            .on_thread_stop_functor  = nullptr}) -> std::shared_ptr<thread_pool>;

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
     * @param uint64_t The number of tasks resumed, if any where null they are discarded.
     */
    template<coro::concepts::range_of<std::coroutine_handle<>> range_type>
    auto resume(const range_type& handles) noexcept -> uint64_t
    {
        m_size.fetch_add(std::size(handles), std::memory_order::release);

        size_t null_handles{0};

        {
            std::scoped_lock lk{m_wait_mutex};
            for (const auto& handle : handles)
            {
                if (handle != nullptr) [[likely]]
                {
                    m_queue.emplace_back(handle);
                }
                else
                {
                    ++null_handles;
                }
            }
        }

        if (null_handles > 0)
        {
            m_size.fetch_sub(null_handles, std::memory_order::release);
        }

        uint64_t total = std::size(handles) - null_handles;
        if (total >= m_threads.size())
        {
            m_wait_cv.notify_all();
        }
        else
        {
            for (uint64_t i = 0; i < total; ++i)
            {
                m_wait_cv.notify_one();
            }
        }

        return total;
    }

    /**
     * Immediately yields the current task and places it at the end of the queue of tasks waiting
     * to be processed.  This will immediately be picked up again once it naturally goes through the
     * FIFO task queue.  This function is useful to yielding long processing tasks to let other tasks
     * get processing time.
     */
    [[nodiscard]] auto yield() -> schedule_operation { return schedule(); }

    /**
     * Shutsdown the thread pool.  This will finish any tasks scheduled prior to calling this
     * function but will prevent the thread pool from scheduling any new tasks.  This call is
     * blocking and will wait until all inflight tasks are completed before returnin.
     */
    auto shutdown() noexcept -> void;

    /**
     * @return The number of tasks waiting in the task queue + the executing tasks.
     */
    auto size() const noexcept -> std::size_t { return m_size.load(std::memory_order::acquire); }

    /**
     * @return True if the task queue is empty and zero tasks are currently executing.
     */
    auto empty() const noexcept -> bool { return size() == 0; }

    /**
     * @return The number of tasks waiting in the task queue to be executed.
     */
    auto queue_size() const noexcept -> std::size_t
    {
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
    /// The background executor threads.
    std::vector<std::thread> m_threads;
    /// Mutex for executor threads to sleep on the condition variable.
    std::mutex m_wait_mutex;
    /// Condition variable for each executor thread to wait on when no tasks are available.
    std::condition_variable_any m_wait_cv;
    /// FIFO queue of tasks waiting to be executed.
    std::deque<std::coroutine_handle<>> m_queue;

    /**
     * Each background thread runs from this function.
     * @param idx The executor's idx for internal data structure accesses.
     */
    auto executor(std::size_t idx) -> void;
    /**
     * @param handle Schedules the given coroutine to be executed upon the first available thread.
     */
    auto schedule_impl(std::coroutine_handle<> handle) noexcept -> void;

    /// The number of tasks in the queue + currently executing.
    std::atomic<std::size_t> m_size{0};
    /// Has the thread pool been requested to shut down?
    std::atomic<bool> m_shutdown_requested{false};
};

} // namespace coro

#pragma once

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
#else
    #include "coro/thread_pool.hpp"
#endif

namespace coro
{

/**
 * Facade for universal access to default scheduler or default thread pool.
 * This facade supports the concept of coro::concepts::executor.
 */
class facade
{
public:
    facade();

    /**
     * Getting a single instance of facade per process
     * @return single instance of facade
     */
    static facade* instance();

#ifdef LIBCORO_FEATURE_NETWORKING
    [[nodiscard]] auto schedule() -> coro::io_scheduler::schedule_operation;
#else
    [[nodiscard]] auto schedule() -> coro::thread_pool::operation;
#endif

    bool               spawn(coro::task<void>&& task) noexcept;
    [[nodiscard]] auto yield() { return schedule(); };
    bool               resume(std::coroutine_handle<> handle);
    std::size_t        size();
    bool               empty();
    void               shutdown();

    /**
     * Schedules a task on the thread pool or io_scheduler and returns another task that must be awaited on for
     * completion. This can be done via co_await in a coroutine context or coro::sync_wait() outside of coroutine
     * context.
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

#ifdef LIBCORO_FEATURE_NETWORKING
    /**
     * Set up default coro::io_scheduler::options before constructing a single instance of the facade
     * @param io_scheduler_options io_scheduler options
     */
    static void set_io_scheduler_options(io_scheduler::options io_scheduler_options);

    /**
     * Get default coro::io_scheduler
     */
    std::shared_ptr<io_scheduler> get_io_scheduler();
#else
    /**
     * Set up default coro::thread_pool::options before constructing a single instance of the facade
     * @param thread_pool_options thread_pool options
     */
    static void set_thread_pool_options(thread_pool::options thread_pool_options);

    /**
     * Get default coro::thread_pool
     */
    std::shared_ptr<thread_pool> get_thread_pool();
#endif

private:
#ifdef LIBCORO_FEATURE_NETWORKING
    /**
     * Options for default coro::io_scheduler
     */
    static io_scheduler::options s_io_scheduler_options;

    /**
     * Default coro::io_scheduler
     */
    std::shared_ptr<io_scheduler> m_io_scheduler;
#else

    /**
     * Options for default coro::thread_pool
     */
    static thread_pool::options s_thread_pool_options;

    /**
     * Default coro::thread_pool
     */
    std::shared_ptr<thread_pool> m_thread_pool;
#endif
};

} // namespace coro

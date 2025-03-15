#pragma once

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
#else
    #include "coro/thread_pool.hpp"
#endif

namespace coro
{

class facade
{
public:
    facade();
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
    static void                   set_io_scheduler_options(io_scheduler::options io_scheduler_options);
    std::shared_ptr<io_scheduler> get_io_scheduler();
#else
    static void                  set_thread_pool_options(thread_pool::options thread_pool_options);
    std::shared_ptr<thread_pool> get_thread_pool();
#endif

private:
#ifdef LIBCORO_FEATURE_NETWORKING
    static io_scheduler::options  s_io_scheduler_options;
    std::shared_ptr<io_scheduler> m_io_scheduler;
#else
    static thread_pool::options  s_thread_pool_options;
    std::shared_ptr<thread_pool> m_thread_pool;
#endif
};

} // namespace coro

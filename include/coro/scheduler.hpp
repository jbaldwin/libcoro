#pragma once

#include "coro/detail/poll_info.hpp"
#include "coro/detail/timer_handle.hpp"
#include "coro/expected.hpp"
#include "coro/fd.hpp"
#include "coro/io_notifier.hpp"
#include "coro/poll.hpp"
#include "coro/thread_pool.hpp"
#include <type_traits>
#include <unistd.h>

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/net/socket.hpp"
#endif

#include "detail/pipe.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include <unistd.h>

namespace coro
{
enum timeout_status
{
    no_timeout,
    timeout,
};

class scheduler
{
    using timed_events = detail::poll_info::timed_events;

    struct private_constructor
    {
        explicit private_constructor() = default;
    };

public:
    class schedule_operation;
    friend schedule_operation;

    enum class thread_strategy_t
    {
        /// Spawns a dedicated background thread for the scheduler to run on.
        spawn,
        /// Requires the user to call process_events() to drive the scheduler.
        manual
    };

    enum class execution_strategy_t
    {
        /// Tasks will be FIFO queued to be executed on a thread pool.  This is better for tasks that
        /// are long lived and will use lots of CPU because long lived tasks will block other i/o
        /// operations while they complete.  This strategy is generally better for lower latency
        /// requirements at the cost of throughput.
        process_tasks_on_thread_pool,
        /// Tasks will be executed inline on the io scheduler thread.  This is better for short tasks
        /// that can be quickly processed and not block other i/o operations for very long.  This
        /// strategy is generally better for higher throughput at the cost of latency.
        process_tasks_inline
    };

    struct options
    {
        /// Should the io scheduler spawn a dedicated event processor?
        thread_strategy_t thread_strategy{thread_strategy_t::spawn};
        /// If spawning a dedicated event processor a functor to call upon that thread starting.
        std::function<void()> on_io_thread_start_functor{nullptr};
        /// If spawning a dedicated event processor a functor to call upon that thread stopping.
        std::function<void()> on_io_thread_stop_functor{nullptr};
        /// Thread pool options for the task processor threads.  See thread pool for more details.
        thread_pool::options pool{
            .thread_count = ((std::thread::hardware_concurrency() > 1) ? (std::thread::hardware_concurrency() - 1) : 1),
            .on_thread_start_functor = nullptr,
            .on_thread_stop_functor  = nullptr};

        /// If inline task processing is enabled then the io worker will resume tasks on its thread
        /// rather than scheduling them to be picked up by the thread pool.
        execution_strategy_t execution_strategy{execution_strategy_t::process_tasks_on_thread_pool};
    };

    /**
     * @see scheduler::make_unique
     */
    explicit scheduler(options&& opts, private_constructor);

    /**
     * @brief Creates an scheduler executor.
     *
     * @param opts The scheduler's options.
     * @return std::unique_ptr<scheduler>
     */
    static auto make_unique(
        options opts = options{
            .thread_strategy            = thread_strategy_t::spawn,
            .on_io_thread_start_functor = nullptr,
            .on_io_thread_stop_functor  = nullptr,
            .pool =
                {.thread_count =
                     ((std::thread::hardware_concurrency() > 1) ? (std::thread::hardware_concurrency() - 1) : 1),
                 .on_thread_start_functor = nullptr,
                 .on_thread_stop_functor  = nullptr},
            .execution_strategy = execution_strategy_t::process_tasks_on_thread_pool}) -> std::unique_ptr<scheduler>;

    scheduler(const scheduler&)                    = delete;
    scheduler(scheduler&&)                         = delete;
    auto operator=(const scheduler&) -> scheduler& = delete;
    auto operator=(scheduler&&) -> scheduler&      = delete;

    virtual ~scheduler();

    /**
     * Given a thread_strategy_t::manual this function should be called at regular intervals to
     * process events that are ready.  If a using thread_strategy_t::spawn this is run continously
     * on a dedicated background thread and does not need to be manually invoked.
     * @param timeout If no events are ready how long should the function wait for events to be ready?
     *                Passing zero (default) for the timeout will check for any events that are
     *                ready now, and then return.  This could be zero events.  Passing -1 means block
     *                indefinitely until an event happens.
     * @return The number of tasks currently executing or waiting to execute.
     */
    auto process_events(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> std::size_t;

    class schedule_operation
    {
        friend class scheduler;
        explicit schedule_operation(scheduler& scheduler) noexcept : m_scheduler(scheduler) {}

    public:
        /**
         * Operations always pause so the executing thread can be switched.
         */
        auto await_ready() noexcept -> bool { return false; }

        /**
         * Suspending always returns to the caller (using void return of await_suspend()) and
         * stores the coroutine internally for the executing thread to resume from.
         */
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
        {
            if (m_scheduler.m_opts.execution_strategy == execution_strategy_t::process_tasks_inline)
            {
                m_scheduler.m_size.fetch_add(1, std::memory_order::release);
                {
                    std::scoped_lock lk{m_scheduler.m_scheduled_tasks_mutex};
                    m_scheduler.m_scheduled_tasks.emplace_back(awaiting_coroutine);
                }

                // Trigger the event to wake-up the scheduler if this event isn't currently triggered.
                bool expected{false};
                if (m_scheduler.m_schedule_pipe_triggered.compare_exchange_strong(
                        expected, true, std::memory_order::release, std::memory_order::relaxed))
                {
                    constexpr int control = 1;
                    ssize_t written = ::write(
                        m_scheduler.m_schedule_pipe.write_fd(),
                        reinterpret_cast<const void*>(&control),
                        sizeof(control));
                    if (written != sizeof(control))
                    {
                        std::cerr << "libcoro::scheduler::schedule_operation failed to write to schedule pipe, bytes written=" << written << "\n";
                    }
                }
            }
            else
            {
                m_scheduler.m_thread_pool->resume(awaiting_coroutine);
            }
        }

        /**
         * no-op as this is the function called first by the thread pool's executing thread.
         */
        auto await_resume() noexcept -> void {}

    private:
        /// The thread pool that this operation will execute on.
        scheduler& m_scheduler;
    };

    /**
     * Schedules the current task onto this scheduler for execution.
     */
    auto schedule() -> schedule_operation { return schedule_operation{*this}; }

    /**
     * Spawns a task into the scheduler and moves ownership of the task to the scheduler.
     * Only void return type tasks can be spawned in this manner since the task submitter will no
     * longer have control over the spawned task, it is effectively detached.
     * @param task The task to execute on this scheduler.  It's lifetime ownership will be transferred
     *             to this scheduler.
     * @return True if the task was succesfully spawned onto the scheduler. This can fail if the task
     *         is already completed or does not contain a valid coroutine anymore.
     */
    auto spawn_detached(coro::task<void>&& task) -> bool;

    /**
     * Spawns the given task to be run on this scheduler, the task returned must be joined in the future.
     * @note The returned task shouldn't be co_await'ed immediately, the spawned task is started on the scheduler
     *       automatically but the returned join task *must* be co_await'ed at some point in the future.
     *       If you drop the returned task it will hang the thread until the spawned task completes so it is
     *       highly advisable to co_await the returned join task appropriately.
     * @param task The task to spawn onto the scheduler.
     * @return A task that can be co_await'ed (joined) in the future to know when the spawned task is complete.
     */
    auto spawn_joinable(coro::task<void>&& task) -> coro::task<void>;

    /**
     * Schedules a task on the scheduler and returns another task that must be awaited on for completion.
     * This can be done via co_await in a coroutine context or coro::sync_wait() outside of coroutine context.
     * @tparam return_type The return value of the task.
     * @param task The task to schedule on the scheduler.
     * @return The task to await for the input task to complete.
     */
    template<typename return_type>
    [[nodiscard]] auto schedule(coro::task<return_type> task) -> coro::task<return_type>
    {
        co_await schedule();
        co_return co_await task;
    }

    /**
     * Schedules a task on the scheduler that must complete within the given timeout.
     * NOTE: This version of schedule does *NOT* cancel the given task, it will continue executing even if it times
     * out. It is absolutely recommended to use the version of this schedule() function that takes an
     * std::stop_token and have the scheduled task check to see if its been cancelled due to timeout to not waste
     * resources.
     * @tparam return_type The return value of the task.
     * @param task The task to schedule on the scheduler with the given timeout.
     * @param timeout How long should this task be given to complete before it times out?
     * @return The task to await for the input task to complete.
     */
    template<typename return_type, typename rep, typename period>
    [[nodiscard]] auto schedule(coro::task<return_type> task, std::chrono::duration<rep, period> timeout)
        -> coro::task<coro::expected<return_type, timeout_status>>
    {
        using namespace std::chrono_literals;

        // If negative or 0 timeout, just schedule the task as normal.
        auto timeout_ms = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(timeout), 0ms);
        if (timeout_ms == 0ms)
        {
            if constexpr (std::is_void_v<return_type>)
            {
                co_await schedule(std::move(task));
                co_return coro::expected<return_type, timeout_status>();
            }
            else
            {
                co_return coro::expected<return_type, timeout_status>(co_await schedule(std::move(task)));
            }
        }

        auto result = co_await when_any(std::move(task), make_timeout_task(timeout_ms));
        if (!std::holds_alternative<timeout_status>(result))
        {
            if constexpr (std::is_void_v<return_type>)
            {
                co_return coro::expected<return_type, timeout_status>();
            }
            else
            {
                co_return coro::expected<return_type, timeout_status>(std::move(std::get<0>(result)));
            }
        }
        else
        {
            co_return coro::unexpected<timeout_status>(std::move(std::get<1>(result)));
        }
    }

#ifndef EMSCRIPTEN
    /**
     * Schedules a task on the scheduler that must complete within the given timeout.
     * NOTE: This version of the task will have the stop_source.request_stop() be called if the timeout triggers.
     *       It is up to you to check in the scheduled task if the stop has been requested to actually stop
     * executing the task.
     * @tparam return_type The return value of the task.
     * @param task The task to schedule on the scheduler with the given timeout.
     * @param timeout How long should this task be given to complete before it times out?
     * @return The task to await for the input task to complete.
     */
    template<typename return_type, typename rep, typename period>
    [[nodiscard]] auto
        schedule(std::stop_source stop_source, coro::task<return_type> task, std::chrono::duration<rep, period> timeout)
            -> coro::task<coro::expected<return_type, timeout_status>>
    {
        using namespace std::chrono_literals;

        // If negative or 0 timeout, just schedule the task as normal.
        auto timeout_ms = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(timeout), 0ms);
        if (timeout_ms == 0ms)
        {
            if constexpr (std::is_void_v<return_type>)
            {
                co_return coro::expected<return_type, timeout_status>();
            }
            else
            {
                co_return coro::expected<return_type, timeout_status>(co_await schedule(std::move(task)));
            }
        }

        auto result = co_await when_any(std::move(stop_source), std::move(task), make_timeout_task(timeout_ms));
        if (!std::holds_alternative<timeout_status>(result))
        {
            if constexpr (std::is_void_v<return_type>)
            {
                co_return coro::expected<return_type, timeout_status>();
            }
            else
            {
                co_return coro::expected<return_type, timeout_status>(std::move(std::get<0>(result)));
            }
        }
        else
        {
            co_return coro::unexpected<timeout_status>(std::move(std::get<1>(result)));
        }
    }
#endif

    /**
     * Schedules the current task to run after the given amount of time has elapsed.
     * @param amount The amount of time to wait before resuming execution of this task.
     *               Given zero or negative amount of time this behaves identical to schedule().
     */
    template<class rep_type, class period_type>
    [[nodiscard]] auto schedule_after(std::chrono::duration<rep_type, period_type> amount) -> coro::task<void>
    {
        return yield_for_internal(std::chrono::duration_cast<std::chrono::nanoseconds>(amount));
    }

    /**
     * Schedules the current task to run at a given time point in the future.
     * @param time The time point to resume execution of this task.  Given 'now' or a time point
     *             in the past this behaves identical to schedule().
     */
    [[nodiscard]] auto schedule_at(time_point time) -> coro::task<void>;

    /**
     * Yields the current task to the end of the queue of waiting tasks.
     */
    [[nodiscard]] auto yield() -> schedule_operation { return schedule_operation{*this}; };

    /**
     * Yields the current task for the given amount of time.
     * @param amount The amount of time to yield for before resuming executino of this task.
     *               Given zero or negative amount of time this behaves identical to yield().
     */
    template<class rep_type, class period_type>
    [[nodiscard]] auto yield_for(std::chrono::duration<rep_type, period_type> amount) -> coro::task<void>
    {
        return yield_for_internal(std::chrono::duration_cast<std::chrono::nanoseconds>(amount));
    }

    /**
     * Yields the current task until the given time point in the future.
     * @param time The time point to resume execution of this task.  Given 'now' or a time point in the
     *             in the past this behaves identical to yield().
     */
    [[nodiscard]] auto yield_until(time_point time) -> coro::task<void>;

    /**
     * Polls the given file descriptor for the given operations.
     * @param fd The file descriptor to poll for events.
     * @param op The operations to poll for.
     * @param timeout The amount of time to wait for the events to trigger.  A timeout of zero will
     *                block indefinitely until the event triggers.
     * @return The result of the poll operation.
     */
    [[nodiscard]] auto poll(
        fd_t                           fd,
        coro::poll_op                  op,
        std::chrono::milliseconds      timeout        = std::chrono::milliseconds{0},
        std::optional<poll_stop_token> cancel_trigger = std::nullopt) -> coro::task<poll_status>;

#ifdef LIBCORO_FEATURE_NETWORKING
    /**
     * Polls the given coro::net::socket for the given operations.
     * @param sock The socket to poll for events on.
     * @param op The operations to poll for.
     * @param timeout The amount of time to wait for the events to trigger.  A timeout of zero will
     *                block indefinitely until the event triggers.
     * @return THe result of the poll operation.
     */
    [[nodiscard]] auto poll(
        const net::socket&             sock,
        coro::poll_op                  op,
        std::chrono::milliseconds      timeout        = std::chrono::milliseconds{0},
        std::optional<poll_stop_token> cancel_trigger = std::nullopt) -> coro::task<poll_status>
    {
        return poll(sock.native_handle(), op, timeout, cancel_trigger);
    }
#endif

    /**
     * Resumes execution of a direct coroutine handle on this io scheduler.
     * @param handle The coroutine handle to resume execution.
     */
    auto resume(std::coroutine_handle<> handle) -> bool;

    template<coro::concepts::sized_range_of<std::coroutine_handle<>> range_type>
    auto resume(const range_type& handles) noexcept -> std::size_t
    {
        auto        size = std::size(handles);
        std::size_t invalid_handles{0};
        for (const auto& handle : handles)
        {
            if (!resume(handle))
            {
                ++invalid_handles;
            }
        }

        return size - invalid_handles;
    }

    /**
     * @return The number of tasks waiting in the task queue + the executing tasks.
     */
    auto size() const noexcept -> std::size_t
    {
        if (m_opts.execution_strategy == execution_strategy_t::process_tasks_inline)
        {
            return m_size.load(std::memory_order::acquire);
        }
        else
        {
            return m_size.load(std::memory_order::acquire) + m_thread_pool->size();
        }
    }

    /**
     * @return True if the task queue is empty and zero tasks are currently executing.
     */
    auto empty() const noexcept -> bool { return size() == 0; }

    /**
     * Starts the shutdown of the io scheduler.  All currently executing and pending tasks will complete
     * prior to shutting down.  This call is blocking and will not return until all tasks complete.
     */
    auto shutdown() noexcept -> void;

    [[nodiscard]] auto is_shutdown() const -> bool { return m_shutdown_requested.load(std::memory_order::acquire); }

    auto io_notifier() -> io_notifier& { return m_io_notifier; }

private:
    /// The configuration options.
    options m_opts;

    /// The io event notifier.
    ::coro::io_notifier m_io_notifier;
    /// The timer handle for timed events, e.g. yield_for() or scheduler_after().
    detail::timer_handle m_timer;
    /// The event loop pipe to trigger a shutdown.
    detail::pipe_t m_shutdown_pipe{};
    /// The event loop schedule task pipe.
    detail::pipe_t    m_schedule_pipe{};
    std::atomic<bool> m_schedule_pipe_triggered{false};

    /// The number of tasks executing or awaiting events in this io scheduler.
    std::atomic<std::size_t> m_size{0};

    /// The background io worker threads.
    std::thread m_io_thread;
    /// Thread pool for executing tasks when not in inline mode.
    std::unique_ptr<thread_pool> m_thread_pool{nullptr};

    std::mutex m_timed_events_mutex{};
    /// The map of time point's to poll infos for tasks that are yielding for a period of time
    /// or for tasks that are polling with timeouts.
    timed_events m_timed_events{};

    /// Has the scheduler been requested to shut down?
    std::atomic<bool> m_shutdown_requested{false};

    auto yield_for_internal(std::chrono::nanoseconds amount) -> coro::task<void>;

    std::atomic<bool> m_io_processing{false};
    auto              process_events_manual(std::chrono::milliseconds timeout) -> void;
    auto              process_events_dedicated_thread() -> void;
    auto              process_events_execute(std::chrono::milliseconds timeout) -> void;
    static auto       event_to_poll_status(uint32_t events) -> poll_status;

    auto                                 process_scheduled_execute_inline() -> void;
    std::mutex                           m_scheduled_tasks_mutex{};
    std::vector<std::coroutine_handle<>> m_scheduled_tasks{};

    static constexpr const int   m_shutdown_object{0};
    static constexpr const void* m_shutdown_ptr = &m_shutdown_object;

    static constexpr const int   m_timer_object{0};
    static constexpr const void* m_timer_ptr = &m_timer_object;

    static constexpr const int   m_schedule_object{0};
    static constexpr const void* m_schedule_ptr = &m_schedule_object;

    static const constexpr std::chrono::milliseconds              m_default_timeout{1000};
    static const constexpr std::chrono::milliseconds              m_no_timeout{0};
    static const constexpr std::size_t                            m_max_events = 16;
    std::vector<std::pair<detail::poll_info*, coro::poll_status>> m_recent_events{};
    std::vector<std::coroutine_handle<>>                          m_handles_to_resume{};

    auto process_event_execute(detail::poll_info* pi, poll_status status) -> void;
    auto process_timeout_execute() -> void;

    auto add_timer_token(time_point tp, detail::poll_info& pi) -> timed_events::iterator;
    auto remove_timer_token(timed_events::iterator pos) -> void;
    auto update_timeout(time_point now) -> void;

    auto make_timeout_task(std::chrono::milliseconds timeout) -> coro::task<timeout_status>
    {
        co_await schedule_after(timeout);
        co_return timeout_status::timeout;
    }
};

} // namespace coro

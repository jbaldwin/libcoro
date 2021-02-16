#pragma once

#include "coro/event.hpp"
#include "coro/net/socket.hpp"
#include "coro/poll.hpp"
#include "coro/thread_pool.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <thread>
#include <vector>

namespace coro
{
namespace detail
{
class poll_info;
} // namespace detail

class io_scheduler : public coro::thread_pool
{
    friend detail::poll_info;

    using clock        = std::chrono::steady_clock;
    using time_point   = clock::time_point;
    using timed_events = std::multimap<time_point, detail::poll_info*>;

public:
    using fd_t = int;

    enum class thread_strategy_t
    {
        /// Spawns a background thread for the scheduler to run on.
        spawn,
        /// Requires the user to call process_events() to drive the scheduler
        manual
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
    };

    explicit io_scheduler(
        options opts = options{
            .thread_strategy            = thread_strategy_t::spawn,
            .on_io_thread_start_functor = nullptr,
            .on_io_thread_stop_functor  = nullptr,
            .pool                       = {
                .thread_count =
                    ((std::thread::hardware_concurrency() > 1) ? (std::thread::hardware_concurrency() - 1) : 1),
                .on_thread_start_functor = nullptr,
                .on_thread_stop_functor  = nullptr}});

    io_scheduler(const io_scheduler&) = delete;
    io_scheduler(io_scheduler&&)      = delete;
    auto operator=(const io_scheduler&) -> io_scheduler& = delete;
    auto operator=(io_scheduler&&) -> io_scheduler& = delete;

    virtual ~io_scheduler() override;

    /**
     * Given a thread_strategy_t::manual this function should be called at regular intervals to
     * process events that are ready.  If a using thread_strategy_t::spawn this is run continously
     * on a dedicated background thread and does not need to be manually invoked.
     * @param timeout If no events are ready how long should the function wait for events to be ready?
     *                Passing zero (default) for the timeout will check for any events that are
     *                ready now, and then return.  This could be zero events.  Passing -1 means block
     *                indefinitely until an event happens.
     * @param return The number of tasks currently executing or waiting to execute.
     */
    auto process_events(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> std::size_t;

    /**
     * Schedules the current task to run after the given amount of time has elapsed.
     * @param amount The amount of time to wait before resuming execution of this task.
     *               Given zero or negative amount of time this behaves identical to schedule().
     */
    [[nodiscard]] auto schedule_after(std::chrono::milliseconds amount) -> coro::task<void>;

    /**
     * Schedules the current task to run at a given time point in the future.
     * @param time The time point to resume execution of this task.  Given 'now' or a time point
     *             in the past this behaves identical to schedule().
     */
    [[nodiscard]] auto schedule_at(time_point time) -> coro::task<void>;

    /**
     * Yields the current task for the given amount of time.
     * @param amount The amount of time to yield for before resuming executino of this task.
     *               Given zero or negative amount of time this behaves identical to yield().
     */
    [[nodiscard]] auto yield_for(std::chrono::milliseconds amount) -> coro::task<void>;

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
    [[nodiscard]] auto poll(fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>;

    /**
     * Polls the given coro::net::socket for the given operations.
     * @param sock The socket to poll for events on.
     * @param op The operations to poll for.
     * @param timeout The amount of time to wait for the events to trigger.  A timeout of zero will
     *                block indefinitely until the event triggers.
     * @return THe result of the poll operation.
     */
    [[nodiscard]] auto poll(
        const net::socket& sock, coro::poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>
    {
        return poll(sock.native_handle(), op, timeout);
    }

    /**
     * Starts the shutdown of the io scheduler.  All currently executing and pending tasks will complete
     * prior to shutting down.
     * @param wait_for_tasks Given shutdown_t::sync this function will block until all oustanding
     *                       tasks are completed.  Given shutdown_t::async this function will trigger
     *                       the shutdown process but return immediately.  In this case the io_scheduler's
     *                       destructor will block if any background threads haven't joined.
     */
    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) noexcept -> void override;

private:
    /// The configuration options.
    options m_opts;

    /// The event loop epoll file descriptor.
    fd_t m_epoll_fd{-1};
    /// The event loop fd to trigger a shutdown.
    fd_t m_shutdown_fd{-1};
    /// The event loop timer fd for timed events, e.g. yield_for() or scheduler_after().
    fd_t m_timer_fd{-1};

    /// The background io worker threads.
    std::thread m_io_thread;

    std::mutex m_timed_events_mutex{};
    /// The map of time point's to poll infos for tasks that are yielding for a period of time
    /// or for tasks that are polling with timeouts.
    timed_events m_timed_events{};

    std::atomic<bool> m_io_processing{false};
    auto              process_events_manual(std::chrono::milliseconds timeout) -> void;
    auto              process_events_dedicated_thread() -> void;
    auto              process_events_execute(std::chrono::milliseconds timeout) -> void;
    static auto       event_to_poll_status(uint32_t events) -> poll_status;

    auto process_event_execute(detail::poll_info* pi, poll_status status) -> void;
    auto process_timeout_execute() -> void;

    auto add_timer_token(time_point tp, detail::poll_info& pi) -> timed_events::iterator;
    auto remove_timer_token(timed_events::iterator pos) -> void;
    auto update_timeout(time_point now) -> void;

    static constexpr const int   m_shutdown_object{0};
    static constexpr const void* m_shutdown_ptr = &m_shutdown_object;

    static constexpr const int   m_timer_object{0};
    static constexpr const void* m_timer_ptr = &m_timer_object;

    static const constexpr std::chrono::milliseconds m_default_timeout{1000};
    static const constexpr std::chrono::milliseconds m_no_timeout{0};
    static const constexpr std::size_t               m_max_events = 8;
    std::array<struct epoll_event, m_max_events>     m_events{};
};

} // namespace coro

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
        thread_strategy_t     thread_strategy{thread_strategy_t::spawn};
        std::function<void()> on_io_thread_start_functor{nullptr};
        std::function<void()> on_io_thread_stop_functor{nullptr};
        thread_pool::options  pool{
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

    auto process_events(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) -> std::size_t;

    [[nodiscard]] auto schedule_after(std::chrono::milliseconds amount) -> coro::task<void>;
    [[nodiscard]] auto schedule_at(time_point time) -> coro::task<void>;

    [[nodiscard]] auto yield_for(std::chrono::milliseconds amount) -> coro::task<void>;
    [[nodiscard]] auto yield_until(time_point time) -> coro::task<void>;

    [[nodiscard]] auto poll(fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>;

    [[nodiscard]] auto poll(
        const net::socket& sock, coro::poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>
    {
        return poll(sock.native_handle(), op, timeout);
    }

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

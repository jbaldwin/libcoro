#include "coro/io_scheduler.hpp"
#include "coro/detail/task_self_deleting.hpp"

#include <atomic>
#include <cstring>
#include <optional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace coro
{
io_scheduler::io_scheduler(options&& opts, private_constructor)
    : m_opts(opts),
      m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
      m_shutdown_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      m_timer_fd(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      m_schedule_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))

{
}

auto io_scheduler::make_shared(options opts) -> std::shared_ptr<io_scheduler>
{
    auto s = std::make_shared<io_scheduler>(std::move(opts), private_constructor{});

    if (opts.execution_strategy == execution_strategy_t::process_tasks_on_thread_pool)
    {
        s->m_thread_pool = std::make_unique<thread_pool>(std::move(s->m_opts.pool));
    }

    epoll_event e{};
    e.events = EPOLLIN;

    e.data.ptr = const_cast<void*>(m_shutdown_ptr);
    epoll_ctl(s->m_epoll_fd, EPOLL_CTL_ADD, s->m_shutdown_fd, &e);

    e.data.ptr = const_cast<void*>(m_timer_ptr);
    epoll_ctl(s->m_epoll_fd, EPOLL_CTL_ADD, s->m_timer_fd, &e);

    e.data.ptr = const_cast<void*>(m_schedule_ptr);
    epoll_ctl(s->m_epoll_fd, EPOLL_CTL_ADD, s->m_schedule_fd, &e);

    if (s->m_opts.thread_strategy == thread_strategy_t::spawn)
    {
        s->m_io_thread = std::thread([s]() { s->process_events_dedicated_thread(); });
    }
    // else manual mode, the user must call process_events.

    return s;
}

io_scheduler::~io_scheduler()
{
    shutdown();

    if (m_io_thread.joinable())
    {
        m_io_thread.join();
    }

    if (m_epoll_fd != -1)
    {
        close(m_epoll_fd);
        m_epoll_fd = -1;
    }
    if (m_timer_fd != -1)
    {
        close(m_timer_fd);
        m_timer_fd = -1;
    }
    if (m_schedule_fd != -1)
    {
        close(m_schedule_fd);
        m_schedule_fd = -1;
    }
}

auto io_scheduler::process_events(std::chrono::milliseconds timeout) -> std::size_t
{
    process_events_manual(timeout);
    return size();
}

auto io_scheduler::spawn(coro::task<void>&& task) -> bool
{
    m_size.fetch_add(1, std::memory_order::release);
    auto owned_task = detail::make_task_self_deleting(std::move(task));
    owned_task.promise().executor_size(m_size);
    return resume(owned_task.handle());
}

auto io_scheduler::schedule_after(std::chrono::milliseconds amount) -> coro::task<void>
{
    return yield_for(amount);
}

auto io_scheduler::schedule_at(time_point time) -> coro::task<void>
{
    return yield_until(time);
}

auto io_scheduler::yield_for(std::chrono::milliseconds amount) -> coro::task<void>
{
    if (amount <= 0ms)
    {
        co_await schedule();
    }
    else
    {
        // Yield/timeout tasks are considered live in the scheduler and must be accounted for. Note
        // that if the user gives an invalid amount and schedule() is directly called it will account
        // for the scheduled task there.
        m_size.fetch_add(1, std::memory_order::release);

        // Yielding does not requiring setting the timer position on the poll info since
        // it doesn't have a corresponding 'event' that can trigger, it always waits for
        // the timeout to occur before resuming.

        detail::poll_info pi{};
        add_timer_token(clock::now() + amount, pi);
        co_await pi;

        m_size.fetch_sub(1, std::memory_order::release);
    }
    co_return;
}

auto io_scheduler::yield_until(time_point time) -> coro::task<void>
{
    auto now = clock::now();

    // If the requested time is in the past (or now!) bail out!
    if (time <= now)
    {
        co_await schedule();
    }
    else
    {
        m_size.fetch_add(1, std::memory_order::release);

        auto amount = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);

        detail::poll_info pi{};
        add_timer_token(now + amount, pi);
        co_await pi;

        m_size.fetch_sub(1, std::memory_order::release);
    }
    co_return;
}

auto io_scheduler::poll(fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout) -> coro::task<poll_status>
{
    // Because the size will drop when this coroutine suspends every poll needs to undo the subtraction
    // on the number of active tasks in the scheduler.  When this task is resumed by the event loop.
    m_size.fetch_add(1, std::memory_order::release);

    // Setup two events, a timeout event and the actual poll for op event.
    // Whichever triggers first will delete the other to guarantee only one wins.
    // The resume token will be set by the scheduler to what the event turned out to be.

    bool timeout_requested = (timeout > 0ms);

    detail::poll_info pi{};
    pi.m_fd = fd;

    if (timeout_requested)
    {
        pi.m_timer_pos = add_timer_token(clock::now() + timeout, pi);
    }

    epoll_event e{};
    e.events   = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLRDHUP;
    e.data.ptr = &pi;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &e) == -1)
    {
        std::cerr << "epoll ctl error on fd " << fd << "\n";
    }

    // The event loop will 'clean-up' whichever event didn't win since the coroutine is scheduled
    // onto the thread poll its possible the other type of event could trigger while its waiting
    // to execute again, thus restarting the coroutine twice, that would be quite bad.
    auto result = co_await pi;
    m_size.fetch_sub(1, std::memory_order::release);
    co_return result;
}

auto io_scheduler::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (m_shutdown_requested.exchange(true, std::memory_order::acq_rel) == false)
    {
        if (m_thread_pool != nullptr)
        {
            m_thread_pool->shutdown();
        }

        // Signal the event loop to stop asap, triggering the event fd is safe.
        uint64_t value{1};
        auto     written = ::write(m_shutdown_fd, &value, sizeof(value));
        (void)written;

        if (m_io_thread.joinable())
        {
            m_io_thread.join();
        }
    }
}

auto io_scheduler::process_events_manual(std::chrono::milliseconds timeout) -> void
{
    bool expected{false};
    if (m_io_processing.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
    {
        process_events_execute(timeout);
        m_io_processing.exchange(false, std::memory_order::release);
    }
}

auto io_scheduler::process_events_dedicated_thread() -> void
{
    if (m_opts.on_io_thread_start_functor != nullptr)
    {
        m_opts.on_io_thread_start_functor();
    }

    m_io_processing.exchange(true, std::memory_order::release);
    // Execute tasks until stopped or there are no more tasks to complete.
    while (!m_shutdown_requested.load(std::memory_order::acquire) || size() > 0)
    {
        process_events_execute(m_default_timeout);
    }
    m_io_processing.exchange(false, std::memory_order::release);

    if (m_opts.on_io_thread_stop_functor != nullptr)
    {
        m_opts.on_io_thread_stop_functor();
    }
}

auto io_scheduler::process_events_execute(std::chrono::milliseconds timeout) -> void
{
    auto event_count = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, timeout.count());
    if (event_count > 0)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(event_count); ++i)
        {
            epoll_event& event      = m_events[i];
            void*        handle_ptr = event.data.ptr;

            if (handle_ptr == m_timer_ptr)
            {
                // Process all events that have timed out.
                process_timeout_execute();
            }
            else if (handle_ptr == m_schedule_ptr)
            {
                // Process scheduled coroutines.
                process_scheduled_execute_inline();
            }
            else if (handle_ptr == m_shutdown_ptr) [[unlikely]]
            {
                // Nothing to do , just needed to wake-up and smell the flowers
            }
            else
            {
                // Individual poll task wake-up.
                process_event_execute(static_cast<detail::poll_info*>(handle_ptr), event_to_poll_status(event.events));
            }
        }
    }

    // Its important to not resume any handles until the full set is accounted for.  If a timeout
    // and an event for the same handle happen in the same epoll_wait() call then inline processing
    // will destruct the poll_info object before the second event is handled.  This is also possible
    // with thread pool processing, but probably has an extremely low chance of occuring due to
    // the thread switch required.  If m_max_events == 1 this would be unnecessary.

    if (!m_handles_to_resume.empty())
    {
        if (m_opts.execution_strategy == execution_strategy_t::process_tasks_inline)
        {
            for (auto& handle : m_handles_to_resume)
            {
                handle.resume();
            }
        }
        else
        {
            m_thread_pool->resume(m_handles_to_resume);
        }

        m_handles_to_resume.clear();
    }
}

auto io_scheduler::event_to_poll_status(uint32_t events) -> poll_status
{
    if (events & EPOLLIN || events & EPOLLOUT)
    {
        return poll_status::event;
    }
    else if (events & EPOLLERR)
    {
        return poll_status::error;
    }
    else if (events & EPOLLRDHUP || events & EPOLLHUP)
    {
        return poll_status::closed;
    }

    throw std::runtime_error{"invalid epoll state"};
}

auto io_scheduler::process_scheduled_execute_inline() -> void
{
    std::vector<std::coroutine_handle<>> tasks{};
    {
        // Acquire the entire list, and then reset it.
        std::scoped_lock lk{m_scheduled_tasks_mutex};
        tasks.swap(m_scheduled_tasks);

        // Clear the schedule eventfd if this is a scheduled task.
        eventfd_t value{0};
        eventfd_read(m_schedule_fd, &value);

        // Clear the in memory flag to reduce eventfd_* calls on scheduling.
        m_schedule_fd_triggered.exchange(false, std::memory_order::release);
    }

    // This set of handles can be safely resumed now since they do not have a corresponding timeout event.
    for (auto& task : tasks)
    {
        task.resume();
    }
    m_size.fetch_sub(tasks.size(), std::memory_order::release);
}

auto io_scheduler::process_event_execute(detail::poll_info* pi, poll_status status) -> void
{
    if (!pi->m_processed)
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        // Its possible the event and the timeout occurred in the same epoll, make sure only one
        // is ever processed, the other is discarded.
        pi->m_processed = true;

        // Given a valid fd always remove it from epoll so the next poll can blindly EPOLL_CTL_ADD.
        if (pi->m_fd != -1)
        {
            epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, pi->m_fd, nullptr);
        }

        // Since this event triggered, remove its corresponding timeout if it has one.
        if (pi->m_timer_pos.has_value())
        {
            remove_timer_token(pi->m_timer_pos.value());
        }

        pi->m_poll_status = status;

        while (pi->m_awaiting_coroutine == nullptr)
        {
            std::atomic_thread_fence(std::memory_order::acquire);
        }

        m_handles_to_resume.emplace_back(pi->m_awaiting_coroutine);
    }
}

auto io_scheduler::process_timeout_execute() -> void
{
    std::vector<detail::poll_info*> poll_infos{};
    auto                            now = clock::now();

    {
        std::scoped_lock lk{m_timed_events_mutex};
        while (!m_timed_events.empty())
        {
            auto first    = m_timed_events.begin();
            auto [tp, pi] = *first;

            if (tp <= now)
            {
                m_timed_events.erase(first);
                poll_infos.emplace_back(pi);
            }
            else
            {
                break;
            }
        }
    }

    for (auto pi : poll_infos)
    {
        if (!pi->m_processed)
        {
            // Its possible the event and the timeout occurred in the same epoll, make sure only one
            // is ever processed, the other is discarded.
            pi->m_processed = true;

            // Since this timed out, remove its corresponding event if it has one.
            if (pi->m_fd != -1)
            {
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, pi->m_fd, nullptr);
            }

            while (pi->m_awaiting_coroutine == nullptr)
            {
                std::atomic_thread_fence(std::memory_order::acquire);
                // std::cerr << "process_event_execute() has a nullptr event\n";
            }

            m_handles_to_resume.emplace_back(pi->m_awaiting_coroutine);
            pi->m_poll_status = coro::poll_status::timeout;
        }
    }

    // Update the time to the next smallest time point, re-take the current now time
    // since updating and resuming tasks could shift the time.
    update_timeout(clock::now());
}

auto io_scheduler::add_timer_token(time_point tp, detail::poll_info& pi) -> timed_events::iterator
{
    std::scoped_lock lk{m_timed_events_mutex};
    auto             pos = m_timed_events.emplace(tp, &pi);

    // If this item was inserted as the smallest time point, update the timeout.
    if (pos == m_timed_events.begin())
    {
        update_timeout(clock::now());
    }

    return pos;
}

auto io_scheduler::remove_timer_token(timed_events::iterator pos) -> void
{
    {
        std::scoped_lock lk{m_timed_events_mutex};
        auto             is_first = (m_timed_events.begin() == pos);

        m_timed_events.erase(pos);

        // If this was the first item, update the timeout.  It would be acceptable to just let it
        // also fire the timeout as the event loop will ignore it since nothing will have timed
        // out but it feels like the right thing to do to update it to the correct timeout value.
        if (is_first)
        {
            update_timeout(clock::now());
        }
    }
}

auto io_scheduler::update_timeout(time_point now) -> void
{
    if (!m_timed_events.empty())
    {
        auto& [tp, pi] = *m_timed_events.begin();

        auto amount = tp - now;

        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(amount);
        amount -= seconds;
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(amount);

        // As a safeguard if both values end up as zero (or negative) then trigger the timeout
        // immediately as zero disarms timerfd according to the man pages and negative values
        // will result in an error return value.
        if (seconds <= 0s)
        {
            seconds = 0s;
            if (nanoseconds <= 0ns)
            {
                // just trigger "immediately"!
                nanoseconds = 1ns;
            }
        }

        itimerspec ts{};
        ts.it_value.tv_sec  = seconds.count();
        ts.it_value.tv_nsec = nanoseconds.count();

        if (timerfd_settime(m_timer_fd, 0, &ts, nullptr) == -1)
        {
            std::cerr << "Failed to set timerfd errorno=[" << std::string{strerror(errno)} << "].";
        }
    }
    else
    {
        // Setting these values to zero disables the timer.
        itimerspec ts{};
        ts.it_value.tv_sec  = 0;
        ts.it_value.tv_nsec = 0;
        if (timerfd_settime(m_timer_fd, 0, &ts, nullptr) == -1)
        {
            std::cerr << "Failed to set timerfd errorno=[" << std::string{strerror(errno)} << "].";
        }
    }
}

} // namespace coro

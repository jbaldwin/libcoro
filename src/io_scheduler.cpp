#include "coro/io_scheduler.hpp"

#include <iostream>

namespace coro
{
detail::resume_token_base::resume_token_base(io_scheduler* s) noexcept : m_scheduler(s), m_state(nullptr)
{
}

detail::resume_token_base::resume_token_base(resume_token_base&& other)
{
    m_scheduler = other.m_scheduler;
    m_state     = other.m_state.exchange(nullptr);

    other.m_scheduler = nullptr;
}

auto detail::resume_token_base::awaiter::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    const void* const set_state = &m_token;

    m_awaiting_coroutine = awaiting_coroutine;

    // This value will update if other threads write to it via acquire.
    void* old_value = m_token.m_state.load(std::memory_order::acquire);
    do
    {
        // Resume immediately if already in the set state.
        if (old_value == set_state)
        {
            return false;
        }

        m_next = static_cast<awaiter*>(old_value);
    } while (!m_token.m_state.compare_exchange_weak(
        old_value, this, std::memory_order::release, std::memory_order::acquire));

    return true;
}

auto detail::resume_token_base::reset() noexcept -> void
{
    void* old_value = this;
    m_state.compare_exchange_strong(old_value, nullptr, std::memory_order::acquire);
}

auto detail::resume_token_base::operator=(resume_token_base&& other) -> resume_token_base&
{
    if (std::addressof(other) != this)
    {
        m_scheduler = other.m_scheduler;
        m_state     = other.m_state.exchange(nullptr);

        other.m_scheduler = nullptr;
    }

    return *this;
}

io_scheduler::task_manager::task_manager(const std::size_t reserve_size, const double growth_factor)
    : m_growth_factor(growth_factor)
{
    m_tasks.resize(reserve_size);
    for (std::size_t i = 0; i < reserve_size; ++i)
    {
        m_task_indexes.emplace_back(i);
    }
    m_free_pos = m_task_indexes.begin();
}

auto io_scheduler::task_manager::store(coro::task<void> user_task) -> task<void>&
{
    // Only grow if completely full and attempting to add more.
    if (m_free_pos == m_task_indexes.end())
    {
        m_free_pos = grow();
    }

    // Store the task inside a cleanup task for self deletion.
    auto index     = *m_free_pos;
    m_tasks[index] = make_cleanup_task(std::move(user_task), m_free_pos);

    // Mark the current used slot as used.
    std::advance(m_free_pos, 1);

    return m_tasks[index];
}

auto io_scheduler::task_manager::gc() -> std::size_t
{
    std::size_t deleted{0};
    if (!m_tasks_to_delete.empty())
    {
        for (const auto& pos : m_tasks_to_delete)
        {
            // This doesn't actually 'delete' the task, it'll get overwritten when a
            // new user task claims the free space.  It could be useful to actually
            // delete the tasks so the coroutine stack frames are destroyed.  The advantage
            // of letting a new task replace and old one though is that its a 1:1 exchange
            // on delete and create, rather than a large pause here to delete all the
            // completed tasks.

            // Put the deleted position at the end of the free indexes list.
            m_task_indexes.splice(m_task_indexes.end(), m_task_indexes, pos);
        }
        deleted = m_tasks_to_delete.size();
        m_tasks_to_delete.clear();
    }
    return deleted;
}

auto io_scheduler::task_manager::grow() -> task_position
{
    // Save an index at the current last item.
    auto        last_pos = std::prev(m_task_indexes.end());
    std::size_t new_size = m_tasks.size() * m_growth_factor;
    for (std::size_t i = m_tasks.size(); i < new_size; ++i)
    {
        m_task_indexes.emplace_back(i);
    }
    m_tasks.resize(new_size);
    // Set the free pos to the item just after the previous last item.
    return std::next(last_pos);
}

auto io_scheduler::task_manager::make_cleanup_task(task<void> user_task, task_position pos) -> task<void>
{
    try
    {
        co_await user_task;
    }
    catch (const std::runtime_error& e)
    {
        // TODO: what would be a good way to report this to the user...?  Catching here is required
        // since the co_await will unwrap the unhandled exception on the task.  The scheduler thread
        // should really not throw unhandled exceptions, otherwise it'll take the application down.
        // The user's task should ideally be wrapped in a catch all and handle it themselves.
        std::cerr << "scheduler user_task had an unhandled exception e.what()= " << e.what() << "\n";
    }

    m_tasks_to_delete.push_back(pos);
    co_return;
}

io_scheduler::io_scheduler(const options opts)
    : m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
      m_accept_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      m_timer_fd(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      m_thread_strategy(opts.thread_strategy),
      m_task_manager(opts.reserve_size, opts.growth_factor)
{
    epoll_event e{};
    e.events = EPOLLIN;

    e.data.ptr = const_cast<void*>(m_accept_ptr);
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_accept_fd, &e);

    e.data.ptr = const_cast<void*>(m_timer_ptr);
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_timer_fd, &e);

    if (m_thread_strategy == thread_strategy_t::spawn)
    {
        m_scheduler_thread = std::thread([this] { process_events_dedicated_thread(); });
    }
    else if (m_thread_strategy == thread_strategy_t::adopt)
    {
        process_events_dedicated_thread();
    }
    // else manual mode, the user must call process_events.
}

io_scheduler::~io_scheduler()
{
    shutdown();
    if (m_epoll_fd != -1)
    {
        close(m_epoll_fd);
        m_epoll_fd = -1;
    }
    if (m_accept_fd != -1)
    {
        close(m_accept_fd);
        m_accept_fd = -1;
    }
    if (m_timer_fd != -1)
    {
        close(m_timer_fd);
        m_timer_fd = -1;
    }
}

auto io_scheduler::schedule(coro::task<void> task) -> bool
{
    if (is_shutdown())
    {
        return false;
    }

    // This function intentionally does not check to see if its executing on the thread that is
    // processing events.  If the given task recursively generates tasks it will result in a
    // stack overflow very quickly.  Instead it takes the long path of adding it to the FIFO
    // queue and processing through the normal pipeline.  This simplifies the code and also makes
    // the order in which newly submitted tasks are more fair in regards to FIFO.

    m_size.fetch_add(1, std::memory_order::relaxed);
    {
        std::lock_guard<std::mutex> lk{m_accept_mutex};
        m_accept_queue.emplace_back(std::move(task));
    }

    // Send an event if one isn't already set.  We use strong here to avoid spurious failures
    // but if it fails due to it actually being set we don't want to retry.
    bool expected{false};
    if (m_event_set.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
    {
        uint64_t value{1};
        ::write(m_accept_fd, &value, sizeof(value));
    }

    return true;
}

auto io_scheduler::schedule(std::vector<task<void>> tasks) -> bool
{
    if (is_shutdown())
    {
        return false;
    }

    m_size.fetch_add(tasks.size(), std::memory_order::relaxed);
    {
        std::lock_guard<std::mutex> lk{m_accept_mutex};
        m_accept_queue.insert(
            m_accept_queue.end(), std::make_move_iterator(tasks.begin()), std::make_move_iterator(tasks.end()));

        // std::move(tasks.begin(), tasks.end(), std::back_inserter(m_accept_queue));
    }

    tasks.clear();

    bool expected{false};
    if (m_event_set.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
    {
        uint64_t value{1};
        ::write(m_accept_fd, &value, sizeof(value));
    }

    return true;
}

auto io_scheduler::schedule_after(coro::task<void> task, std::chrono::milliseconds after) -> bool
{
    if (m_shutdown_requested.load(std::memory_order::relaxed))
    {
        return false;
    }

    return schedule(make_scheduler_after_task(std::move(task), after));
}

auto io_scheduler::schedule_at(coro::task<void> task, time_point time) -> bool
{
    auto now = clock::now();

    // If the requested time is in the past (or now!) bail out!
    if (time <= now)
    {
        return false;
    }

    auto amount = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
    return schedule_after(std::move(task), amount);
}

auto io_scheduler::poll(fd_t fd, poll_op op, std::chrono::milliseconds timeout) -> coro::task<poll_status>
{
    // Setup two events, a timeout event and the actual poll for op event.
    // Whichever triggers first will delete the other to guarantee only one wins.
    // The resume token will be set by the scheduler to what the event turned out to be.

    using namespace std::chrono_literals;
    bool timeout_requested = (timeout > 0ms);

    resume_token<poll_status> token{};
    timer_tokens::iterator    timer_pos;

    if (timeout_requested)
    {
        timer_pos = add_timer_token(clock::now() + timeout, &token);
    }

    epoll_event e{};
    e.events   = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    e.data.ptr = &token;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &e);

    auto status = co_await unsafe_yield<poll_status>(token);
    switch (status)
    {
        // The event triggered first, delete the timeout.
        case poll_status::event:
            if (timeout_requested)
            {
                remove_timer_token(timer_pos);
            }
            break;
        default:
            // Deleting the event is done regardless below in epoll_ctl()
            break;
    }

    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

    co_return status;
}

auto io_scheduler::poll(const net::socket& sock, poll_op op, std::chrono::milliseconds timeout)
    -> coro::task<poll_status>
{
    return poll(sock.native_handle(), op, timeout);
}

auto io_scheduler::read(fd_t fd, std::span<char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<poll_status, ssize_t>>
{
    auto status = co_await poll(fd, poll_op::read, timeout);
    switch (status)
    {
        case poll_status::event:
            co_return {status, ::read(fd, buffer.data(), buffer.size())};
        default:
            co_return {status, 0};
    }
}

auto io_scheduler::read(const net::socket& sock, std::span<char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<poll_status, ssize_t>>
{
    return read(sock.native_handle(), buffer, timeout);
}

auto io_scheduler::write(fd_t fd, const std::span<const char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<poll_status, ssize_t>>
{
    auto status = co_await poll(fd, poll_op::write, timeout);
    switch (status)
    {
        case poll_status::event:
            co_return {status, ::write(fd, buffer.data(), buffer.size())};
        default:
            co_return {status, 0};
    }
}

auto io_scheduler::write(const net::socket& sock, const std::span<const char> buffer, std::chrono::milliseconds timeout)
    -> coro::task<std::pair<poll_status, ssize_t>>
{
    return write(sock.native_handle(), buffer, timeout);
}

auto io_scheduler::yield() -> coro::task<void>
{
    co_await schedule();
    co_return;
}

auto io_scheduler::yield_for(std::chrono::milliseconds amount) -> coro::task<void>
{
    // If the requested amount of time is negative or zero just return.
    using namespace std::chrono_literals;
    if (amount <= 0ms)
    {
        co_return;
    }

    resume_token<poll_status> token{};

    add_timer_token(clock::now() + amount, &token);

    // Wait for the token timer to trigger.
    co_await token;
    co_return;
}

auto io_scheduler::yield_until(time_point time) -> coro::task<void>
{
    auto now = clock::now();

    // If the requested time is in the past (or now!) just return.
    if (time <= now)
    {
        co_return;
    }

    auto amount = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
    co_await yield_for(amount);
    co_return;
}

auto io_scheduler::process_events(std::chrono::milliseconds timeout) -> std::size_t
{
    process_events_external_thread(timeout);
    return m_size.load(std::memory_order::relaxed);
}

auto io_scheduler::shutdown(shutdown_t wait_for_tasks) -> void
{
    if (!m_shutdown_requested.exchange(true, std::memory_order::release))
    {
        // Signal the event loop to stop asap.
        uint64_t value{1};
        ::write(m_accept_fd, &value, sizeof(value));

        if (wait_for_tasks == shutdown_t::sync && m_scheduler_thread.joinable())
        {
            m_scheduler_thread.join();
        }
    }
}

auto io_scheduler::make_scheduler_after_task(coro::task<void> task, std::chrono::milliseconds wait_time)
    -> coro::task<void>
{
    // Wait for the period requested, and then resume their task.
    co_await yield_for(wait_time);
    co_await task;
    co_return;
}

auto io_scheduler::add_timer_token(time_point tp, resume_token<poll_status>* token_ptr) -> timer_tokens::iterator
{
    auto pos = m_timer_tokens.emplace(tp, token_ptr);

    // If this item was inserted as the smallest time point, update the timeout.
    if (pos == m_timer_tokens.begin())
    {
        update_timeout(clock::now());
    }

    return pos;
}

auto io_scheduler::remove_timer_token(timer_tokens::iterator pos) -> void
{
    auto is_first = (m_timer_tokens.begin() == pos);

    m_timer_tokens.erase(pos);

    // If this was the first item, update the timeout.  It would be acceptable to just let it
    // also fire the timeout as the event loop will ignore it since nothing will have timed
    // out but it feels like the right thing to do to update it to the correct timeout value.
    if (is_first)
    {
        update_timeout(clock::now());
    }
}

auto io_scheduler::resume(std::coroutine_handle<> handle) -> void
{
    {
        std::lock_guard<std::mutex> lk{m_accept_mutex};
        m_accept_queue.emplace_back(handle);
    }

    // Signal to the event loop there is a task to resume if one hasn't already been sent.
    bool expected{false};
    if (m_event_set.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
    {
        uint64_t value{1};
        ::write(m_accept_fd, &value, sizeof(value));
    }
}

auto io_scheduler::process_task_and_start(task<void>& task) -> void
{
    m_task_manager.store(std::move(task)).resume();
}

auto io_scheduler::process_task_variant(task_variant& tv) -> void
{
    if (std::holds_alternative<coro::task<void>>(tv))
    {
        auto& task = std::get<coro::task<void>>(tv);
        // Store the users task and immediately start executing it.
        process_task_and_start(task);
    }
    else
    {
        auto handle = std::get<std::coroutine_handle<>>(tv);
        // The cleanup wrapper task will catch all thrown exceptions unconditionally.
        handle.resume();
    }
}

auto io_scheduler::process_task_queue() -> void
{
    std::size_t amount{0};
    {
        std::lock_guard<std::mutex> lk{m_accept_mutex};
        while (!m_accept_queue.empty() && amount < task_inline_process_amount)
        {
            m_processing_tasks[amount] = std::move(m_accept_queue.front());
            m_accept_queue.pop_front();
            ++amount;
        }
    }

    // The queue is empty, we are done here.
    if (amount == 0)
    {
        return;
    }

    for (std::size_t i = 0; i < amount; ++i)
    {
        process_task_variant(m_processing_tasks[i]);
    }
}

auto io_scheduler::process_events_poll_execute(std::chrono::milliseconds user_timeout) -> void
{
    // Need to acquire m_accept_queue size to determine if there are any pending tasks.
    std::atomic_thread_fence(std::memory_order::acquire);
    bool tasks_ready = !m_accept_queue.empty();

    auto timeout = (tasks_ready) ? m_no_timeout : user_timeout;

    // Poll is run every iteration to make sure 'waiting' events are properly put into
    // the FIFO queue for when they are ready.
    auto event_count = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, timeout.count());
    if (event_count > 0)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(event_count); ++i)
        {
            epoll_event& event      = m_events[i];
            void*        handle_ptr = event.data.ptr;

            if (handle_ptr == m_accept_ptr)
            {
                uint64_t value{0};
                ::read(m_accept_fd, &value, sizeof(value));
                (void)value; // discard, the read merely resets the eventfd counter to zero.

                // Let any threads scheduling work know that the event set has been consumed.
                // Important to do this after the accept file descriptor has been read.
                // This needs to succeed so best practice is to loop compare exchange weak.
                bool expected{true};
                while (!m_event_set.compare_exchange_weak(
                    expected, false, std::memory_order::release, std::memory_order::relaxed))
                {
                }

                tasks_ready = true;
            }
            else if (handle_ptr == m_timer_ptr)
            {
                // If the timer fd triggered, loop and call every task that has a wait time <= now.
                while (!m_timer_tokens.empty())
                {
                    // Now is continuously calculated since resuming tasks could take a fairly
                    // significant amount of time and might 'trigger' more timeouts.
                    auto now = clock::now();

                    auto first           = m_timer_tokens.begin();
                    auto [tp, token_ptr] = *first;

                    if (tp <= now)
                    {
                        // Important to erase first so if any timers are updated after resume
                        // this timer won't be taken into account.
                        m_timer_tokens.erase(first);
                        // Every event triggered on the timer tokens is *always* a timeout.
                        token_ptr->resume(poll_status::timeout);
                    }
                    else
                    {
                        break;
                    }
                }

                // Update the time to the next smallest time point, re-take the current now time
                // since processing tasks could shit the time.
                update_timeout(clock::now());
            }
            else
            {
                // Individual poll task wake-up, this will queue the coroutines waiting
                // on the resume token into the FIFO queue for processing.
                auto* token_ptr = static_cast<resume_token<poll_status>*>(handle_ptr);
                token_ptr->resume(event_to_poll_status(event.events));
            }
        }
    }

    if (tasks_ready)
    {
        process_task_queue();
    }

    if (!m_task_manager.delete_tasks_empty())
    {
        m_size.fetch_sub(m_task_manager.gc(), std::memory_order::relaxed);
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

auto io_scheduler::process_events_external_thread(std::chrono::milliseconds user_timeout) -> void
{
    // Do not allow two threads to process events at the same time.
    bool expected{false};
    if (m_running.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
    {
        process_events_poll_execute(user_timeout);
        m_running.exchange(false, std::memory_order::release);
    }
}

auto io_scheduler::process_events_dedicated_thread() -> void
{
    m_running.exchange(true, std::memory_order::release);
    // Execute tasks until stopped or there are more tasks to complete.
    while (!m_shutdown_requested.load(std::memory_order::relaxed) || m_size.load(std::memory_order::relaxed) > 0)
    {
        process_events_poll_execute(m_default_timeout);
    }
    m_running.exchange(false, std::memory_order::release);
}

auto io_scheduler::update_timeout(time_point now) -> void
{
    using namespace std::chrono_literals;
    if (!m_timer_tokens.empty())
    {
        auto& [tp, task] = *m_timer_tokens.begin();

        auto amount = tp - now;

        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(amount);
        amount -= seconds;
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(amount);

        // As a safeguard if both values end up as zero (or negative) then trigger the timeout
        // immediately as zero disarms timerfd according to the man pages and negative valeues
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
            std::string msg = "Failed to set timerfd errorno=[" + std::string{strerror(errno)} + "].";
            throw std::runtime_error(msg.data());
        }
    }
    else
    {
        // Setting these values to zero disables the timer.
        itimerspec ts{};
        ts.it_value.tv_sec  = 0;
        ts.it_value.tv_nsec = 0;
        timerfd_settime(m_timer_fd, 0, &ts, nullptr);
    }
}

} // namespace coro

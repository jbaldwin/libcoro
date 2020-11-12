#pragma once

#include "coro/poll.hpp"
#include "coro/shutdown.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <coroutine>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <thread>
#include <variant>
#include <vector>

#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

namespace coro
{
class io_scheduler;

namespace detail
{
class resume_token_base
{
public:
    resume_token_base(io_scheduler* s) noexcept : m_scheduler(s), m_state(nullptr) {}

    virtual ~resume_token_base() = default;

    resume_token_base(const resume_token_base&) = delete;
    resume_token_base(resume_token_base&& other)
    {
        m_scheduler = other.m_scheduler;
        m_state     = other.m_state.exchange(nullptr);

        other.m_scheduler = nullptr;
    }
    auto operator=(const resume_token_base&) -> resume_token_base& = delete;

    auto operator=(resume_token_base&& other) -> resume_token_base&
    {
        if (std::addressof(other) != this)
        {
            m_scheduler = other.m_scheduler;
            m_state     = other.m_state.exchange(nullptr);

            other.m_scheduler = nullptr;
        }

        return *this;
    }

    bool is_set() const noexcept { return m_state.load(std::memory_order::acquire) == this; }

    struct awaiter
    {
        awaiter(const resume_token_base& token) noexcept : m_token(token) {}

        auto await_ready() const noexcept -> bool { return m_token.is_set(); }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
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

        auto await_resume() noexcept
        {
            // no-op
        }

        const resume_token_base& m_token;
        std::coroutine_handle<>  m_awaiting_coroutine;
        awaiter*                 m_next{nullptr};
    };

    auto operator co_await() const noexcept -> awaiter { return awaiter{*this}; }

    auto reset() noexcept -> void
    {
        void* old_value = this;
        m_state.compare_exchange_strong(old_value, nullptr, std::memory_order::acquire);
    }

protected:
    friend struct awaiter;
    io_scheduler*              m_scheduler{nullptr};
    mutable std::atomic<void*> m_state;
};

} // namespace detail

template<typename return_type>
class resume_token final : public detail::resume_token_base
{
    friend io_scheduler;
    resume_token() : detail::resume_token_base(nullptr) {}
    resume_token(io_scheduler& s) : detail::resume_token_base(&s) {}

public:
    ~resume_token() override = default;

    resume_token(const resume_token&) = delete;
    resume_token(resume_token&&)      = default;
    auto operator=(const resume_token&) -> resume_token& = delete;
    auto operator=(resume_token &&) -> resume_token& = default;

    auto resume(return_type value) noexcept -> void;

    auto return_value() const& -> const return_type& { return m_return_value; }

    auto return_value() && -> return_type&& { return std::move(m_return_value); }

private:
    return_type m_return_value;
};

template<>
class resume_token<void> final : public detail::resume_token_base
{
    friend io_scheduler;
    resume_token() : detail::resume_token_base(nullptr) {}
    resume_token(io_scheduler& s) : detail::resume_token_base(&s) {}

public:
    ~resume_token() override = default;

    resume_token(const resume_token&) = delete;
    resume_token(resume_token&&)      = default;
    auto operator=(const resume_token&) -> resume_token& = delete;
    auto operator=(resume_token &&) -> resume_token& = default;

    auto resume() noexcept -> void;
};

class io_scheduler
{
public:
    using fd_t = int;

private:
    using clock        = std::chrono::steady_clock;
    using time_point   = clock::time_point;
    using task_variant = std::variant<coro::task<void>, std::coroutine_handle<>>;
    using task_queue   = std::deque<task_variant>;

    using timer_tokens = std::multimap<time_point, resume_token<poll_status>*>;

    /// resume_token<T> needs to be able to call internal scheduler::resume()
    template<typename return_type>
    friend class resume_token;

    class task_manager
    {
    public:
        using task_position = std::list<std::size_t>::iterator;

        task_manager(const std::size_t reserve_size, const double growth_factor) : m_growth_factor(growth_factor)
        {
            m_tasks.resize(reserve_size);
            for (std::size_t i = 0; i < reserve_size; ++i)
            {
                m_task_indexes.emplace_back(i);
            }
            m_free_pos = m_task_indexes.begin();
        }

        /**
         * Stores a users task and sets a continuation coroutine to automatically mark the task
         * as deleted upon the coroutines completion.
         * @param user_task The scheduled user's task to store since it has suspended after its
         *                  first execution.
         * @return The task just stored wrapped in the self cleanup task.
         */
        auto store(coro::task<void> user_task) -> task<void>&
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

        /**
         * Garbage collects any tasks that are marked as deleted.
         * @return The number of tasks that were deleted.
         */
        auto gc() -> std::size_t
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

        /**
         * @return The number of tasks that are awaiting deletion.
         */
        auto delete_task_size() const -> std::size_t { return m_tasks_to_delete.size(); }

        /**
         * @return True if there are no tasks awaiting deletion.
         */
        auto delete_tasks_empty() const -> bool { return m_tasks_to_delete.empty(); }

        /**
         * @return The capacity of this task manager before it will need to grow in size.
         */
        auto capacity() const -> std::size_t { return m_tasks.size(); }

    private:
        /**
         * Grows each task container by the growth factor.
         * @return The position of the free index after growing.
         */
        auto grow() -> task_position
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

        /**
         * Encapsulate the users tasks in a cleanup task which marks itself for deletion upon
         * completion.  Simply co_await the users task until its completed and then mark the given
         * position within the task manager as being deletable.  The scheduler's next iteration
         * in its event loop will then free that position up to be re-used.
         *
         * This function will also unconditionally catch all unhandled exceptions by the user's
         * task to prevent the scheduler from throwing exceptions.
         * @param user_task The user's task.
         * @param pos The position where the task data will be stored in the task manager.
         * @return The user's task wrapped in a self cleanup task.
         */
        auto make_cleanup_task(task<void> user_task, task_position pos) -> task<void>
        {
            try
            {
                co_await user_task;
            }
            catch (const std::runtime_error& e)
            {
                std::cerr << "scheduler user_task had an unhandled exception e.what()= " << e.what() << "\n";
            }

            m_tasks_to_delete.push_back(pos);
            co_return;
        }

        /// Maintains the lifetime of the tasks until they are completed.
        std::vector<task<void>> m_tasks{};
        /// The full set of indexes into `m_tasks`.
        std::list<std::size_t> m_task_indexes{};
        /// The set of tasks that have completed and need to be deleted.
        std::vector<task_position> m_tasks_to_delete{};
        /// The current free position within the task indexes list.  Anything before
        /// this point is used, itself and anything after is free.
        task_position m_free_pos{};
        /// The amount to grow the containers by when all spaces are taken.
        double m_growth_factor{};
    };

    static constexpr const int   m_accept_object{0};
    static constexpr const void* m_accept_ptr = &m_accept_object;

    static constexpr const int   m_timer_object{0};
    static constexpr const void* m_timer_ptr = &m_timer_object;

    /**
     * An operation is an awaitable type with a coroutine to resume the task scheduled on one of
     * the executor threads.
     */
    class operation
    {
        friend class io_scheduler;
        /**
         * Only io_schedulers can create operations when a task is being scheduled.
         * @param tp The io scheduler that created this operation.
         */
        explicit operation(io_scheduler& ios) noexcept : m_io_scheduler(ios) {}

    public:
        /**
         * Operations always pause so the executing thread and be switched.
         */
        auto await_ready() noexcept -> bool { return false; }

        /**
         * Suspending always returns to the caller (using void return of await_suspend()) and
         * stores the coroutine internally for the executing thread to resume from.
         */
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
        {
            // m_awaiting_coroutine = awaiting_coroutine;
            m_io_scheduler.resume(awaiting_coroutine);
        }

        /**
         * no-op as this is the function called first by the io_scheduler's executing thread.
         */
        auto await_resume() noexcept -> void {}

    private:
        /// The io_scheduler that this operation will execute on.
        io_scheduler& m_io_scheduler;
        // // The coroutine awaiting execution.
        // std::coroutine_handle<> m_awaiting_coroutine{nullptr};
    };

    /**
     * Schedules the currently executing task onto this io_scheduler, effectively placing it at
     * the end of the FIFO queue.
     * `co_await s.yield()`
     */
    auto schedule() -> operation { return operation{*this}; }

public:
    enum class thread_strategy_t
    {
        /// Spawns a background thread for the scheduler to run on.
        spawn,
        /// Adopts this thread as the scheduler thread.
        adopt,
        /// Requires the user to call process_events() to drive the scheduler
        manual
    };

    struct options
    {
        /// The number of tasks to reserve space for upon creating the scheduler.
        std::size_t reserve_size{8};
        /// The growth factor for task space when capacity is full.
        double growth_factor{2};
        /// The threading strategy.
        thread_strategy_t thread_strategy{thread_strategy_t::spawn};
    };

    /**
     * @param options Various scheduler options to tune how it behaves.
     */
    io_scheduler(const options opts = options{8, 2, thread_strategy_t::spawn})
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

    io_scheduler(const io_scheduler&) = delete;
    io_scheduler(io_scheduler&&)      = delete;
    auto operator=(const io_scheduler&) -> io_scheduler& = delete;
    auto operator=(io_scheduler &&) -> io_scheduler& = delete;

    virtual ~io_scheduler()
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

    /**
     * Schedules a task to be run as soon as possible.  This pushes the task into a FIFO queue.
     * @param task The task to schedule as soon as possible.
     * @return True if the task has been scheduled, false if scheduling failed.  Currently the only
     *         way for this to fail is if the scheduler is trying to shutdown.
     */
    auto schedule(coro::task<void> task) -> bool
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

    template<awaitable_void... tasks_type>
    auto schedule(tasks_type&&... tasks) -> bool
    {
        if (is_shutdown())
        {
            return false;
        }

        m_size.fetch_add(sizeof...(tasks), std::memory_order::relaxed);
        {
            std::lock_guard<std::mutex> lk{m_accept_mutex};
            ((m_accept_queue.emplace_back(std::forward<tasks_type>(tasks))), ...);
        }

        bool expected{false};
        if (m_event_set.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
        {
            uint64_t value{1};
            ::write(m_accept_fd, &value, sizeof(value));
        }

        return true;
    }

    auto schedule(std::vector<task<void>>& tasks)
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

    /**
     * Schedules a task to be run after waiting for a certain period of time.
     * @param task The task to schedule after waiting `after` amount of time.
     * @return True if the task has been scheduled, false if scheduling failed.  Currently the only
     *         way for this to fail is if the scheduler is trying to shutdown.
     */
    auto schedule_after(coro::task<void> task, std::chrono::milliseconds after) -> bool
    {
        if (m_shutdown_requested.load(std::memory_order::relaxed))
        {
            return false;
        }

        return schedule(make_scheduler_after_task(std::move(task), after));
    }

    /**
     * Schedules a task to be run at a specific time in the future.
     * @param task
     * @param time
     * @return True if the task is scheduled.  False if time is in the past or the scheduler is
     *         trying to shutdown.
     */
    auto schedule_at(coro::task<void> task, time_point time) -> bool
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

    /**
     * Polls a specific file descriptor for the given poll operation.
     * @param fd The file descriptor to poll.
     * @param op The type of poll operation to perform.
     * @param timeout The timeout for this poll operation, if timeout <= 0 then poll will block
     *                indefinitely until the event is triggered.
     */
    auto poll(fd_t fd, poll_op op, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        -> coro::task<poll_status>
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

    /**
     * This function will first poll the given `fd` to make sure it can be read from.  Once notified
     * that the `fd` has data available to read the given `buffer` is filled with up to the buffer's
     * size of data.  The number of bytes read is returned.
     * @param fd The file desriptor to read from.
     * @param buffer The buffer to place read bytes into.
     * @param timeout The timeout for the read operation, if timeout <= 0 then read will block
     *                indefinitely until the event is triggered.
     * @return The number of bytes read or an error code if negative.
     */
    auto read(fd_t fd, std::span<char> buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
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

    /**
     * This function will first poll the given `fd` to make sure it can be written to.  Once notified
     * that the `fd` can be written to the given buffer's contents are written to the `fd`.
     * @param fd The file descriptor to write the contents of `buffer` to.
     * @param buffer The data to write to `fd`.
     * @return The number of bytes written or an error code if negative.
     */
    auto write(fd_t fd, const std::span<const char> buffer) -> coro::task<std::pair<poll_status, ssize_t>>
    {
        auto status = co_await poll(fd, poll_op::write);
        switch (status)
        {
            case poll_status::event:
                co_return {status, ::write(fd, buffer.data(), buffer.size())};
            default:
                co_return {status, 0};
        }
    }

    /**
     * Immediately yields the current task and places it at the end of the queue of tasks waiting
     * to be processed.  This will immediately be picked up again once it naturally goes through the
     * FIFO task queue.  This function is useful to yielding long processing tasks to let other tasks
     * get processing time.
     */
    auto yield() -> coro::task<void>
    {
        co_await schedule();
        co_return;
    }

    /**
     * Immediately yields the current task and provides a resume token to resume this yielded
     * coroutine when the async operation has completed.
     *
     * Normal usage of this might look like:
     *  \code
           scheduler.yield([](coro::resume_token<void>& t) {
               auto on_service_complete = [&]() {
                   t.resume();  // This resume call will resume the task on the scheduler thread.
               };
               service.do_work(on_service_complete);
           });
     *  \endcode
     * The above example will yield the current task and then through the 3rd party service's
     * on complete callback function let the scheduler know that it should resume execution of the task.
     *
     * @tparam func Functor to invoke with the yielded coroutine handle to be resumed.
     * @param f Immediately invoked functor with the yield point coroutine handle to resume with.
     * @return A task to co_await until the manual `scheduler::resume(handle)` is called.
     */
    template<typename return_type, std::invocable<resume_token<return_type>&> before_functor>
    auto yield(before_functor before) -> coro::task<return_type>
    {
        resume_token<return_type> token{*this};
        before(token);
        co_await token;
        if constexpr (std::is_same_v<return_type, void>)
        {
            co_return;
        }
        else
        {
            co_return token.return_value();
        }
    }

    /**
     * User provided resume token to yield the current coroutine until the token's resume is called.
     * Its also possible to co_await a resume token inline without calling this yield function.
     * @param token Resume token to await the current task on.  Use scheduer::generate_resume_token<T>() to
     *              generate a resume token to use on this scheduer.
     */
    template<typename return_type>
    auto yield(resume_token<return_type>& token) -> coro::task<void>
    {
        co_await token;
        co_return;
    }

    /**
     * Yields the current coroutine for `amount` of time.
     * @param amount The amount of time to wait.
     */
    auto yield_for(std::chrono::milliseconds amount) -> coro::task<void>
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

    /**
     * Yields the current coroutine until `time`.  If time is in the past this function will
     * return immediately.
     * @param time The time point in the future to yield until.
     */
    auto yield_until(time_point time) -> coro::task<void>
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

    /**
     * Generates a resume token that can be used to resume an executing task.
     * @tparam The return type of resuming the async operation.
     * @return Resume token with the given return type.
     */
    template<typename return_type = void>
    auto generate_resume_token() -> resume_token<return_type>
    {
        return resume_token<return_type>(*this);
    }

    /**
     * If runnint in mode thread_strategy_t::manual this function must be called at regular
     * intervals to process events on the io_scheduler.  This function will do nothing in any
     * other thread_strategy_t mode.
     * @param timeout The timeout to wait for events.
     * @return The number of executing tasks.
     */
    auto process_events(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) -> std::size_t
    {
        process_events_external_thread(timeout);
        return m_size.load(std::memory_order::relaxed);
    }

    /**
     * @return The number of active tasks still executing and unprocessed submitted tasks.
     */
    auto size() const -> std::size_t { return m_size.load(std::memory_order::relaxed); }

    /**
     * @return True if there are no tasks executing or waiting to be executed in this scheduler.
     */
    auto empty() const -> bool { return size() == 0; }

    /**
     * @return The maximum number of tasks this scheduler can process without growing.
     */
    auto capacity() const -> std::size_t { return m_task_manager.capacity(); }

    /**
     * Is there a thread processing this schedulers events?
     * If this is in thread strategy spawn or adopt this will always be true until shutdown.
     */
    auto is_running() const noexcept -> bool { return m_running.load(std::memory_order::relaxed); }

    /**
     * @return True if this scheduler has been requested to shutdown.
     */
    auto is_shutdown() const noexcept -> bool { return m_shutdown_requested.load(std::memory_order::relaxed); }

    /**
     * Requests the scheduler to finish processing all of its current tasks and shutdown.
     * New tasks submitted via `scheduler::schedule()` will be rejected after this is called.
     * @param wait_for_tasks This call will block until all tasks are complete if shutdown_t::sync
     *                       is passed in, if shutdown_t::async is passed this function will tell
     *                       the scheduler to shutdown but not wait for all tasks to complete, it returns
     *                       immediately.
     */
    virtual auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) -> void
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

private:
    /// The event loop epoll file descriptor.
    fd_t m_epoll_fd{-1};
    /// The event loop accept new tasks and resume tasks file descriptor.
    fd_t m_accept_fd{-1};
    /// The event loop timer fd for timed events, e.g. yield_for() or scheduler_after().
    fd_t m_timer_fd{-1};

    /// The map of time point's to resume tokens for tasks that are yielding for a period of time
    /// or for tasks that are polling with timeouts.
    timer_tokens m_timer_tokens;

    /// The threading strategy this scheduler is using.
    thread_strategy_t m_thread_strategy;
    /// Is this scheduler currently running? Manual mode might not always be running.
    std::atomic<bool> m_running{false};
    /// Has the scheduler been requested to shutdown?
    std::atomic<bool> m_shutdown_requested{false};
    /// If running in threading mode spawn the background thread to process events.
    std::thread m_scheduler_thread;

    /// FIFO queue for new and resumed tasks to execute.
    task_queue m_accept_queue{};
    std::mutex m_accept_mutex{};

    /// Has a thread sent an event? (E.g. avoid a kernel write/read?).
    std::atomic<bool> m_event_set{false};

    /// The total number of tasks that are being processed or suspended.
    std::atomic<std::size_t> m_size{0};

    /// The maximum number of tasks to process inline before polling for more tasks.
    static constexpr const std::size_t task_inline_process_amount{64};
    /// Pre-allocated memory area for tasks to process.
    std::array<task_variant, task_inline_process_amount> m_processing_tasks;

    task_manager m_task_manager;

    auto make_scheduler_after_task(coro::task<void> task, std::chrono::milliseconds wait_time) -> coro::task<void>
    {
        // Wait for the period requested, and then resume their task.
        co_await yield_for(wait_time);
        co_await task;
        co_return;
    }

    template<typename return_type>
    auto unsafe_yield(resume_token<return_type>& token) -> coro::task<return_type>
    {
        co_await token;
        if constexpr (std::is_same_v<return_type, void>)
        {
            co_return;
        }
        else
        {
            co_return token.return_value();
        }
    }

    auto add_timer_token(time_point tp, resume_token<poll_status>* token_ptr) -> timer_tokens::iterator
    {
        auto pos = m_timer_tokens.emplace(tp, token_ptr);

        // If this item was inserted as the smallest time point, update the timeout.
        if (pos == m_timer_tokens.begin())
        {
            update_timeout(clock::now());
        }

        return pos;
    }

    auto remove_timer_token(timer_tokens::iterator pos) -> void
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

    auto resume(std::coroutine_handle<> handle) -> void
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

    static constexpr std::chrono::milliseconds   m_default_timeout{1000};
    static constexpr std::chrono::milliseconds   m_no_timeout{0};
    static constexpr std::size_t                 m_max_events = 8;
    std::array<struct epoll_event, m_max_events> m_events{};

    auto process_task_and_start(task<void>& task) -> void { m_task_manager.store(std::move(task)).resume(); }

    inline auto process_task_variant(task_variant& tv) -> void
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

    auto process_task_queue() -> void
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

    auto process_events_poll_execute(std::chrono::milliseconds user_timeout) -> void
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
                    {}

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

    auto event_to_poll_status(uint32_t events) -> poll_status
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

    auto process_events_external_thread(std::chrono::milliseconds user_timeout) -> void
    {
        // Do not allow two threads to process events at the same time.
        bool expected{false};
        if (m_running.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
        {
            process_events_poll_execute(user_timeout);
            m_running.exchange(false, std::memory_order::release);
        }
    }

    auto process_events_dedicated_thread() -> void
    {
        m_running.exchange(true, std::memory_order::release);
        // Execute tasks until stopped or there are more tasks to complete.
        while (!m_shutdown_requested.load(std::memory_order::relaxed) || m_size.load(std::memory_order::relaxed) > 0)
        {
            process_events_poll_execute(m_default_timeout);
        }
        m_running.exchange(false, std::memory_order::release);
    }

    auto update_timeout(time_point now) -> void
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
                    // just trigger immediately!
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
};

template<typename return_type>
inline auto resume_token<return_type>::resume(return_type value) noexcept -> void
{
    void* old_value = m_state.exchange(this, std::memory_order::acq_rel);
    if (old_value != this)
    {
        m_return_value = std::move(value);

        auto* waiters = static_cast<awaiter*>(old_value);
        while (waiters != nullptr)
        {
            // Intentionally not checking if this is running on the scheduler process event thread
            // as it can create a stack overflow if it triggers a 'resume chain'.  unsafe_yield()
            // is guaranteed in this context to never be recursive and thus resuming directly
            // on the process event thread should not be able to trigger a stack overflow.

            auto* next = waiters->m_next;
            // If scheduler is nullptr this is an unsafe_yield()
            // If scheduler is present this is a yield()
            if (m_scheduler == nullptr) // || m_scheduler->this_thread_is_processing_events())
            {
                waiters->m_awaiting_coroutine.resume();
            }
            else
            {
                m_scheduler->resume(waiters->m_awaiting_coroutine);
            }
            waiters = next;
        }
    }
}

inline auto resume_token<void>::resume() noexcept -> void
{
    void* old_value = m_state.exchange(this, std::memory_order::acq_rel);
    if (old_value != this)
    {
        auto* waiters = static_cast<awaiter*>(old_value);
        while (waiters != nullptr)
        {
            auto* next = waiters->m_next;
            if (m_scheduler == nullptr)
            {
                waiters->m_awaiting_coroutine.resume();
            }
            else
            {
                m_scheduler->resume(waiters->m_awaiting_coroutine);
            }
            waiters = next;
        }
    }
}

} // namespace coro

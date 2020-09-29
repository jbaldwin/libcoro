#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <span>
#include <type_traits>
#include <list>
#include <queue>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

#include <atomic>
#include <coroutine>
#include <optional>

#include <iostream>

namespace coro
{

class scheduler;

namespace detail
{
class resume_token_base
{
public:
    resume_token_base(scheduler* eng) noexcept
        :   m_scheduler(eng),
            m_state(nullptr)
    {
    }

    virtual ~resume_token_base() = default;

    resume_token_base(const resume_token_base&) = delete;
    resume_token_base(resume_token_base&& other)
    {
        m_scheduler = other.m_scheduler;
        m_state = other.m_state.exchange(0);

        other.m_scheduler = nullptr;

    }
    auto operator=(const resume_token_base&) -> resume_token_base& = delete;
    auto operator=(resume_token_base&& other) -> resume_token_base&
    {
        if(std::addressof(other) != this)
        {
            m_scheduler = other.m_scheduler;
            m_state = other.m_state.exchange(0);

            other.m_scheduler = nullptr;
        }

        return *this;
    }

    bool is_set() const noexcept
    {
        return m_state.load(std::memory_order_acquire) == this;
    }

    struct awaiter
    {
        awaiter(const resume_token_base& token) noexcept
            : m_token(token)
        {

        }

        auto await_ready() const noexcept -> bool
        {
            return m_token.is_set();
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            const void* const set_state = &m_token;

            m_awaiting_coroutine = awaiting_coroutine;

            // This value will update if other threads write to it via acquire.
            void* old_value = m_token.m_state.load(std::memory_order_acquire);
            do
            {
                // Resume immediately if already in the set state.
                if(old_value == set_state)
                {
                    return false;
                }

                m_next = static_cast<awaiter*>(old_value);
            } while(!m_token.m_state.compare_exchange_weak(
                old_value,
                this,
                std::memory_order_release,
                std::memory_order_acquire));

            return true;
        }

        auto await_resume() noexcept
        {
            // no-op
        }

        const resume_token_base& m_token;
        std::coroutine_handle<> m_awaiting_coroutine;
        awaiter* m_next{nullptr};
    };

    auto operator co_await() const noexcept -> awaiter
    {
        return awaiter{*this};
    }

    auto reset() noexcept -> void
    {
        void* old_value = this;
        m_state.compare_exchange_strong(old_value, nullptr, std::memory_order_acquire);
    }

protected:
    friend struct awaiter;
    scheduler* m_scheduler{nullptr};
    mutable std::atomic<void*> m_state;
};

} // namespace detail

template<typename return_type>
class resume_token final : public detail::resume_token_base
{
    friend scheduler;
    resume_token()
        : detail::resume_token_base(nullptr)
    {

    }
    resume_token(scheduler& s)
        : detail::resume_token_base(&s)
    {

    }
public:

    ~resume_token() override = default;

    resume_token(const resume_token&) = delete;
    resume_token(resume_token&&) = default;
    auto operator=(const resume_token&) -> resume_token& = delete;
    auto operator=(resume_token&&) -> resume_token& = default;

    auto resume(return_type result) noexcept -> void;

    auto result() const & -> const return_type&
    {
        return m_result;
    }

    auto result() && -> return_type&&
    {
        return std::move(m_result);
    }
private:
    return_type m_result;
};

template<>
class resume_token<void> final : public detail::resume_token_base
{
    friend scheduler;
    resume_token()
        : detail::resume_token_base(nullptr)
    {

    }
    resume_token(scheduler& s)
        : detail::resume_token_base(&s)
    {

    }
public:
    ~resume_token() override = default;

    resume_token(const resume_token&) = delete;
    resume_token(resume_token&&) = default;
    auto operator=(const resume_token&) -> resume_token& = delete;
    auto operator=(resume_token&&) -> resume_token& = default;

    auto resume() noexcept -> void;
};

enum class poll_op
{
    /// Poll for read operations.
    read = EPOLLIN,
    /// Poll for write operations.
    write = EPOLLOUT,
    /// Poll for read and write operations.
    read_write = EPOLLIN | EPOLLOUT
};

class scheduler
{
private:
    /// resume_token<T> needs to be able to call internal scheduler::resume()
    template<typename return_type>
    friend class resume_token;

    struct task_data
    {
        /// The user's task, lifetime is maintained by the scheduler.
        coro::task<void> m_user_task;
        /// The post processing cleanup tasks to remove a completed task from the scheduler.
        coro::task<void> m_cleanup_task;
    };

    class task_manager
    {
    public:
        using task_pos = std::list<std::size_t>::iterator;

        task_manager(const std::size_t reserve_size, const double growth_factor)
            : m_growth_factor(growth_factor)
        {
            m_tasks.resize(reserve_size);
            for(std::size_t i = 0; i < reserve_size; ++i)
            {
                m_task_indexes.emplace_back(i);
            }
            m_free_pos = m_task_indexes.begin();
        }

        auto store(coro::task<void> user_task) -> void
        {
            // Only grow if completely full and attempting to add more.
            if(m_free_pos == m_task_indexes.end())
            {
                m_free_pos = grow();
            }

            // Store the user task with its cleanup task to maintain their lifetimes until completed.
            auto index = *m_free_pos;
            auto& task_data = m_tasks[index];
            task_data.m_user_task = std::move(user_task);
            task_data.m_cleanup_task = cleanup_func(m_free_pos);

            // Attach the cleanup task to be the continuation after the users task.
            task_data.m_user_task.promise().continuation(task_data.m_cleanup_task.handle());

            // Mark the current used slot as used.
            std::advance(m_free_pos, 1);
        }

        auto gc() -> std::size_t
        {
            std::size_t deleted{0};
            if(!m_tasks_to_delete.empty())
            {
                for(const auto& pos : m_tasks_to_delete)
                {
                    // This doesn't actually 'delete' the task, it'll get overwritten by a new user
                    // task claims the free space.

                    // Put the deleted position at the end of the free indexes list.
                    m_task_indexes.splice(m_task_indexes.end(), m_task_indexes, pos);
                }
                deleted = m_tasks_to_delete.size();
                m_tasks_to_delete.clear();
            }
            return deleted;
        }

        auto size() const -> std::size_t { return m_tasks_to_delete.size(); }
        auto empty() const -> bool { return m_tasks_to_delete.empty(); }
        auto capacity() const -> std::size_t { return m_tasks.size(); }

    private:
        auto grow() -> task_pos
        {
            // Save an index at the current last item.
            auto last_pos = std::prev(m_task_indexes.end());
            std::size_t new_size = m_tasks.size() * m_growth_factor;
            for(std::size_t i = m_tasks.size(); i < new_size; ++i)
            {
                m_task_indexes.emplace_back(i);
            }
            m_tasks.resize(new_size);
            // Set the free pos to the item just after the previous last item.
            return std::next(last_pos);
        }

        auto cleanup_func(task_pos pos) -> coro::task<void>
        {
            // Mark this task for deletion, it cannot delete itself.
            m_tasks_to_delete.push_back(pos);
            co_return;
        };

        std::vector<task_data> m_tasks{};
        std::list<std::size_t> m_task_indexes{};
        std::vector<task_pos> m_tasks_to_delete{};
        task_pos m_free_pos{};
        double m_growth_factor{};
    };

    static constexpr const int m_submit_object{0};
    static constexpr const int m_resume_object{0};
    static constexpr const void* m_submit_ptr = &m_submit_object;
    static constexpr const void* m_resume_ptr = &m_resume_object;

public:
    using fd_t = int;

    enum class shutdown_t
    {
        /// Synchronously wait for all tasks to complete when calling shutdown.
        sync,
        /// Asynchronously let tasks finish on the background thread on shutdown.
        async
    };

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
        thread_strategy_t thread_strategy{thread_strategy_t::spawn};
    };

    /**
     * @param options Various scheduler options to tune how it behaves.
     */
    scheduler(
        const options opts = options{8, 2, thread_strategy_t::spawn}
    )
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
            m_resume_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
            m_thread_strategy(opts.thread_strategy),
            m_task_manager(opts.reserve_size, opts.growth_factor)
    {
        struct epoll_event e{};
        e.events = EPOLLIN;

        e.data.ptr = const_cast<void*>(m_submit_ptr);
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_submit_fd, &e);

        e.data.ptr = const_cast<void*>(m_resume_ptr);
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_resume_fd, &e);

        if(m_thread_strategy == thread_strategy_t::spawn)
        {
            m_scheduler_thread = std::thread([this] { this->run(); });
        }
        else if(m_thread_strategy == thread_strategy_t::adopt)
        {
            run();
        }
        // else manual mode, the user must call process_events.
    }

    scheduler(const scheduler&) = delete;
    scheduler(scheduler&&) = delete;
    auto operator=(const scheduler&) -> scheduler& = delete;
    auto operator=(scheduler&&) -> scheduler& = delete;

    ~scheduler()
    {
        shutdown();
        if(m_epoll_fd != -1)
        {
            close(m_epoll_fd);
            m_epoll_fd = -1;
        }
        if(m_submit_fd != -1)
        {
            close(m_submit_fd);
            m_submit_fd = -1;
        }
        if(m_resume_fd != -1)
        {
            close(m_resume_fd);
            m_resume_fd = -1;
        }
    }

    // TODO: try and trigger a stack overflow via chained resume'ed tasks on the process event thread.

    /**
     * Schedules a task to be run as soon as possible.  This pushes the task into a FIFO queue.
     * @param task The task to schedule as soon as possible.
     * @return True if the task has been scheduled, false if scheduling failed.  Currently the only
     *         way for this to fail is if the scheduler is trying to shutdown.
     */
    auto schedule(coro::task<void> task) -> bool
    {
        if(m_shutdown)
        {
            return false;
        }

        // This function intentionally does not check to see if its executing on the thread that is
        // processing events.  If the given task recursively generates tasks it will result in a
        // stack overflow very quickly.  Instead it takes the long path of adding it to the FIFO
        // queue and processing through the normal pipeline.  This simplifies the code and also makes
        // the order in which newly submitted tasks are more fair in regards to FIFO.

        ++m_size;
        {
            std::lock_guard<std::mutex> lock{m_submit_mutex};
            m_submitted_tasks.emplace_back(std::move(task));
        }

        // Signal to the thread processing events there is a newly scheduled task.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));

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
        if(m_shutdown)
        {
            return false;
        }

        return schedule(scheduler_after_func(std::move(task), after));
    }

    /**
     * Polls a specific file descriptor for the given poll operation.
     * @param fd The file descriptor to poll.
     * @param op The type of poll operation to perform.
     */
    auto poll(fd_t fd, poll_op op) -> coro::task<void>
    {
        co_await unsafe_yield<void>(
            [&](resume_token<void>& token)
            {
                struct epoll_event e{};
                e.events = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET;
                e.data.ptr = &token;
                epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &e);
            }
        );

        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    }

    /**
     * This function will first poll the given `fd` to make sure it can be read from.  Once notified
     * that the `fd` has data available to read the given `buffer` is filled with up to the buffer's
     * size of data.  The number of bytes read is returned.
     * @param fd The file desriptor to read from.
     * @param buffer The buffer to place read bytes into.
     * @return The number of bytes read or an error code if negative.
     */
    auto read(fd_t fd, std::span<char> buffer) -> coro::task<ssize_t>
    {
        co_await poll(fd, poll_op::read);
        co_return ::read(fd, buffer.data(), buffer.size());
    }

    /**
     * This function will first poll the given `fd` to make sure it can be written to.  Once notified
     * that the `fd` can be written to the given buffer's contents are written to the `fd`.
     * @param fd The file descriptor to write the contents of `buffer` to.
     * @param buffer The data to write to `fd`.
     * @return The number of bytes written or an error code if negative.
     */
    auto write(fd_t fd, const std::span<const char> buffer) -> coro::task<ssize_t>
    {
        co_await poll(fd, poll_op::write);
        co_return ::write(fd, buffer.data(), buffer.size());;
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
            co_return token.result();
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
     * @throw std::runtime_error If the internal system failed to setup required resources to wait.
     * @param amount The amount of time to wait.
     */
    auto yield_for(std::chrono::milliseconds amount) -> coro::task<void>
    {
        fd_t timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if(timer_fd == -1)
        {
            std::string msg = "Failed to create timerfd errorno=[" + std::string{strerror(errno)} + "].";
            throw std::runtime_error(msg.data());
        }

        struct itimerspec ts{};

        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(amount);
        amount -= seconds;
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(amount);

        ts.it_value.tv_sec = seconds.count();
        ts.it_value.tv_nsec = nanoseconds.count();

        if(timerfd_settime(timer_fd, 0, &ts, nullptr) == -1)
        {
            std::string msg = "Failed to set timerfd errorno=[" + std::string{strerror(errno)} + "].";
            throw std::runtime_error(msg.data());
        }

        uint64_t value{0};
        co_await read(timer_fd, std::span<char>{reinterpret_cast<char*>(&value), sizeof(value)});
        close(timer_fd);
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

    auto process_events(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) -> std::size_t
    {
        process_events_internal_set_thread(timeout);
        return m_size;
    }

    /**
     * @return The number of active tasks still executing and unprocessed submitted tasks.
     */
    auto size() const -> std::size_t { return m_size.load(); }

    /**
     * @return True if there are no tasks executing or waiting to be executed in this scheduler.
     */
    auto empty() const -> bool { return m_size == 0; }

    /**
     * @return The maximum number of tasks this scheduler can process without growing.
     */
    auto capacity() const -> std::size_t { return m_task_manager.capacity(); }

    /**
     * Is there a thread processing this schedulers events?
     * If this is in thread strategy spawn or adopt this will always be true until shutdown.
     */
    auto is_running() const noexcept -> bool { return m_running; }

    /**
     * @return True if this scheduler has been requested to shutdown.
     */
    auto is_shutdown() const noexcept -> bool { return m_shutdown; }

    /**
     * Requests the scheduler to finish processing all of its current tasks and shutdown.
     * New tasks submitted via `scheduler::schedule()` will be rejected after this is called.
     * @param wait_for_tasks This call will block until all tasks are complete if shutdown_t::sync
     *                       is passed in, if shutdown_t::async is passed this function will tell
     *                       the scheduler to shutdown but not wait for all tasks to complete, it returns
     *                       immediately.
     */
    auto shutdown(shutdown_t wait_for_tasks = shutdown_t::sync) -> void
    {
        if(!m_shutdown.exchange(true))
        {
            // Signal the event loop to stop asap.
            uint64_t value{1};
            ::write(m_submit_fd, &value, sizeof(value));

            if(wait_for_tasks == shutdown_t::sync && m_scheduler_thread.joinable())
            {
                m_scheduler_thread.join();
            }
        }
    }

private:
    fd_t m_epoll_fd{-1};
    fd_t m_submit_fd{-1};
    fd_t m_resume_fd{-1};

    thread_strategy_t m_thread_strategy;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdown{false};
    std::thread m_scheduler_thread;

    std::mutex m_submit_mutex{};
    std::deque<coro::task<void>> m_submitted_tasks{};

    std::mutex m_resume_mutex{};
    std::deque<std::coroutine_handle<>> m_resume_tasks{};

    std::atomic<std::size_t> m_size{0};

    task_manager m_task_manager;

    auto scheduler_after_func(coro::task<void> inner_task, std::chrono::milliseconds wait_time) -> coro::task<void>
    {
        // Seems to already be done.
        if(inner_task.is_ready())
        {
            co_return;
        }

        // Wait for the period requested, and then resume their task.
        co_await yield_for(wait_time);
        inner_task.resume();
        if(!inner_task.is_ready())
        {
            m_task_manager.store(std::move(inner_task));
        }
        co_return;
    }

    template<typename return_type, std::invocable<resume_token<return_type>&> before_functor>
    auto unsafe_yield(before_functor before) -> coro::task<return_type>
    {
        resume_token<return_type> e{};
        before(e);
        co_await e;
        if constexpr (std::is_same_v<return_type, void>)
        {
            co_return;
        }
        else
        {
            co_return e.result();
        }
    }

    auto resume(std::coroutine_handle<> handle) -> void
    {
        {
            std::lock_guard<std::mutex> lock{m_resume_mutex};
            m_resume_tasks.emplace_back(handle);
        }

        // Signal to the event loop there is a task to resume.
        uint64_t value{1};
        ::write(m_resume_fd, &value, sizeof(value));
    }

    static constexpr std::chrono::milliseconds m_default_timeout{1000};
    static constexpr std::chrono::milliseconds m_no_timeout{0};
    static constexpr std::size_t m_max_events = 8;
    std::array<struct epoll_event, m_max_events> m_events{};

    auto process_events_internal_set_thread(std::chrono::milliseconds user_timeout) -> void
    {
        // Do not allow two threads to process events at the same time.
        bool expected{false};
        if(m_running.compare_exchange_strong(expected, true))
        {
            process_events_internal(user_timeout);
            m_running = false;
        }
    }

    auto process_events_internal(std::chrono::milliseconds user_timeout) -> void
    {
        auto tasks_available = !m_resume_tasks.empty() || !m_submitted_tasks.empty();
        auto timeout = tasks_available ? m_no_timeout : user_timeout;
        auto event_count = epoll_wait(m_epoll_fd, m_events.data(), m_max_events, timeout.count());

        bool submit_event{false};
        bool resume_event{false};
        if(event_count > 0)
        {
            for(std::size_t i = 0; i < event_count; ++i)
            {
                void* handle_ptr = m_events[i].data.ptr;

                if(handle_ptr == m_submit_ptr)
                {
                    uint64_t value{0};
                    ::read(m_submit_fd, &value, sizeof(value));
                    (void)value; // discard, the read merely resets the eventfd counter to zero.
                    submit_event = true;
                }
                else if(handle_ptr == m_resume_ptr)
                {
                    uint64_t value{0};
                    ::read(m_resume_fd, &value, sizeof(value));
                    (void)value; // discard, the read merely resets the eventfd counter to zero.
                    resume_event = true;
                }
                else
                {
                    // Individual poll task wake-up.
                    auto* token_ptr = static_cast<resume_token<void>*>(handle_ptr);
                    token_ptr->resume();
                }
            }
        }

        if(resume_event || !m_resume_tasks.empty())
        {
            do
            {
                std::coroutine_handle<> handle;
                {
                    std::lock_guard<std::mutex> lock{m_resume_mutex};
                    if(m_resume_tasks.empty())
                    {
                        break;
                    }
                    handle = m_resume_tasks.front();
                    m_resume_tasks.pop_front();
                }

                if(!handle.done())
                {
                    handle.resume();
                }
            } while(!m_resume_tasks.empty());
        }

        // Cleanup any tasks that marked themselves as completed and remove them from
        // the active tasks in the scheduler, do this before starting new tasks that might
        // need slots in the task manager.
        if(!m_task_manager.empty())
        {
            m_size -= m_task_manager.gc();
        }

        // Now flush the submit queue.
        if(submit_event || !m_submitted_tasks.empty())
        {
            while(true)
            {
                std::optional<coro::task<void>> task_opt;
                {
                    std::lock_guard<std::mutex> lock{m_submit_mutex};
                    if(m_submitted_tasks.empty())
                    {
                        break;
                    }
                    task_opt = std::move(m_submitted_tasks.front());
                    m_submitted_tasks.pop_front();
                }

                task_start(task_opt.value());
            }
        }
    }

    auto task_start(coro::task<void>& task) -> void
    {
        if(!task.is_ready()) // sanity check, the user could have manually resumed.
        {
            // Attempt to process the task synchronously before suspending.
            task.resume();

            if(!task.is_ready())
            {
                m_task_manager.store(std::move(task));
                // This task is now suspended waiting for an event.
            }
            else
            {
                // This task completed synchronously.
                --m_size;
            }
        }
        else
        {
            --m_size;
        }
    }

    auto run() -> void
    {
        m_running = true;
        // Execute tasks until stopped or there are more tasks to complete.
        while(!m_shutdown || m_size > 0)
        {
            process_events_internal(m_default_timeout);
        }
        m_running = false;
    }
};

template<typename return_type>
inline auto resume_token<return_type>::resume(return_type result) noexcept -> void
{
    void* old_value = m_state.exchange(this, std::memory_order_acq_rel);
    if(old_value != this)
    {
        m_result = std::move(result);

        auto* waiters = static_cast<awaiter*>(old_value);
        while(waiters != nullptr)
        {
            // Intentionally not checking if this is running on the scheduler process event thread
            // as it can create a stack overflow if it triggers a 'resume chain'.  unsafe_yield()
            // is guaranteed in this context to never be recursive and thus resuming directly
            // on the process event thread should not be able to trigger a stack overflow.

            auto* next = waiters->m_next;
            // If scheduler is nullptr this is an unsafe_yield()
            // If scheduler is present this is a yield()
            if(m_scheduler == nullptr)// || m_scheduler->this_thread_is_processing_events())
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
    void* old_value = m_state.exchange(this, std::memory_order_acq_rel);
    if(old_value != this)
    {
        auto* waiters = static_cast<awaiter*>(old_value);
        while(waiters != nullptr)
        {
            auto* next = waiters->m_next;
            if(m_scheduler == nullptr)
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


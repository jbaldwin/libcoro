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
    resume_token_base(resume_token_base&&) = delete;
    auto operator=(const resume_token_base&) -> resume_token_base& = delete;
    auto operator=(resume_token_base&&) -> resume_token_base& = delete;

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
public:
    resume_token(scheduler& s)
        : detail::resume_token_base(&s)
    {

    }
    ~resume_token() override = default;

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
public:
    resume_token(scheduler& s)
        : detail::resume_token_base(&s)
    {

    }
    ~resume_token() override = default;

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
            auto& task_data = *m_tasks.emplace(
                m_tasks.begin() + index,
                std::move(user_task),
                cleanup_func(m_free_pos));

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
    using fd_type = int;

    enum class shutdown_type
    {
        /// Synchronously wait for all tasks to complete when calling shutdown.
        sync,
        /// Asynchronously let tasks finish on the background thread on shutdown.
        async
    };

    /**
     * @param reserve_size Reserve up-front this many tasks for concurrent execution.  The scheduler
     *                     will also automatically grow this if needed.
     * @param growth_factor The factor to grow by when the internal tasks are full.
     */
    scheduler(
        std::size_t reserve_size = 8,
        double growth_factor = 2
    )
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
            m_resume_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
            m_task_manager(reserve_size, growth_factor)
    {
        struct epoll_event e{};
        e.events = EPOLLIN;

        e.data.ptr = const_cast<void*>(m_submit_ptr);
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_submit_fd, &e);

        e.data.ptr = const_cast<void*>(m_resume_ptr);
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_resume_fd, &e);

        m_background_thread = std::thread([this] { this->run(); });
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

    // TODO:
    // 1) Migrate to use coroutines for schedule() and resume(), use resume_tokens!?
    // 2) schedule_afer(task, chrono<REP, RATIO>)

    auto schedule(coro::task<void> task) -> bool
    {
        if(m_shutdown)
        {
            return false;
        }

        ++m_size;
        {
            std::lock_guard<std::mutex> lock{m_submit_mutex};
            m_submitted_tasks.emplace_back(std::move(task));
        }

        // Signal to the event loop there is a submitted task.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));

        return true;
    }

    auto poll(fd_type fd, poll_op op) -> coro::task<void>
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
    auto read(fd_type fd, std::span<char> buffer) -> coro::task<ssize_t>
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
    auto write(fd_type fd, const std::span<const char> buffer) -> coro::task<ssize_t>
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
               auto on_service_complete = [&]() { t.resume(); };
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
        fd_type timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
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
     * @return The number of active tasks still executing and unprocessed submitted tasks.
     */
    auto size() const -> std::size_t { return m_size.load(); }

    /**
     * @return True if there are no tasks executing or waiting to be executed in this scheduler.
     */
    auto empty() const -> bool { return m_size == 0; }

    /**
     * @return True if this scheduler is currently running.
     */
    auto is_running() const noexcept -> bool { return m_is_running; }

    /**
     * @return True if this scheduler has been requested to shutdown.
     */
    auto is_shutdown() const noexcept -> bool { return m_shutdown; }

    /**
     * Requests the scheduler to finish processing all of its current tasks and shutdown.
     * New tasks submitted via `scheduler::schedule()` will be rejected after this is called.
     * @param wait_for_tasks This call will block until all tasks are complete if shutdown_type::sync
     *                       is passed in, if shutdown_type::async is passed this function will tell
     *                       the scheduler to shutdown but not wait for all tasks to complete, it returns
     *                       immediately.
     */
    auto shutdown(shutdown_type wait_for_tasks = shutdown_type::sync) -> void
    {
        if(!m_shutdown.exchange(true))
        {
            // Signal the event loop to stop asap.
            uint64_t value{1};
            ::write(m_submit_fd, &value, sizeof(value));

            if(wait_for_tasks == shutdown_type::sync && m_background_thread.joinable())
            {
                m_background_thread.join();
            }
        }
    }

    /**
     * @return A unique id to identify this scheduler.
     */
    auto scheduler_id() const -> uint32_t { return m_scheduler_id; }

private:
    static std::atomic<uint32_t> m_scheduler_id_counter;
    const uint32_t m_scheduler_id{m_scheduler_id_counter++};

    fd_type m_epoll_fd{-1};
    fd_type m_submit_fd{-1};
    fd_type m_resume_fd{-1};

    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_shutdown{false};
    std::thread m_background_thread;

    std::mutex m_submit_mutex{};
    std::vector<coro::task<void>> m_submitted_tasks{};

    std::mutex m_resume_mutex{};
    std::vector<std::coroutine_handle<>> m_resume_tasks{};

    std::atomic<std::size_t> m_size{0};

    task_manager m_task_manager;

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

    auto run() -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        constexpr std::chrono::milliseconds timeout{1000};
        constexpr std::size_t max_events = 8;
        std::array<struct epoll_event, max_events> events{};

        // Execute tasks until stopped or there are more tasks to complete.
        while(!m_shutdown || m_size > 0)
        {
            auto event_count = epoll_wait(m_epoll_fd, events.data(), max_events, timeout.count());
            if(event_count > 0)
            {
                for(std::size_t i = 0; i < event_count; ++i)
                {
                    void* handle_ptr = events[i].data.ptr;

                    // if(task_id == submit_id)
                    if(handle_ptr == m_submit_ptr)
                    {
                        uint64_t value{0};
                        ::read(m_submit_fd, &value, sizeof(value));
                        (void)value; // discard, the read merely resets the eventfd counter in the kernel.

                        std::vector<coro::task<void>> submitted_tasks{};
                        {
                            std::lock_guard<std::mutex> lock{m_submit_mutex};
                            submitted_tasks.swap(m_submitted_tasks);
                        }

                        for(coro::task<void>& task : submitted_tasks)
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
                        }
                    }
                    else if(handle_ptr == m_resume_ptr)
                    {
                        std::vector<std::coroutine_handle<>> resume_tasks{};
                        {
                            std::lock_guard<std::mutex> lock{m_resume_mutex};
                            resume_tasks.swap(m_resume_tasks);
                        }

                        for(auto& handle : resume_tasks)
                        {
                            if(!handle.done())
                            {
                                handle.resume();
                            }
                        }
                    }
                    else
                    {
                        // Individual poll task wake-up.
                        auto* token_ptr = static_cast<resume_token<void>*>(handle_ptr);
                        token_ptr->resume();
                    }
                }

                // Cleanup any tasks that marked themselves as completed and remove them from
                // the active tasks in the scheduler.
                m_size -= m_task_manager.gc();
            }
        }

        m_is_running = false;
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
            auto* next = waiters->m_next;
            // If scheduler is nullptr this is an unsafe_yield()
            // If scheduler is present this is a yield()
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

inline auto resume_token<void>::resume() noexcept -> void
{
    void* old_value = m_state.exchange(this, std::memory_order_acq_rel);
    if(old_value != this)
    {
        auto* waiters = static_cast<awaiter*>(old_value);
        while(waiters != nullptr)
        {
            auto* next = waiters->m_next;
            // If scheduler is nullptr this is an unsafe_yield()
            // If scheduler is present this is a yield()
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

inline std::atomic<uint32_t> scheduler::m_scheduler_id_counter{0};

} // namespace coro


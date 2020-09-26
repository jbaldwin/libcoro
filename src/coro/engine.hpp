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

enum class poll_op
{
    read = EPOLLIN,
    write = EPOLLOUT,
    read_write = EPOLLIN | EPOLLOUT
};

class engine
{
public:
    using fd_type = int;

    enum class shutdown_type
    {
        /// Synchronously wait for all tasks to complete when calling shutdown.
        sync,
        /// Asynchronously let tasks finish on the background thread on shutdown.
        async
    };

private:
    static constexpr void* m_submit_ptr = nullptr;

public:
    /**
     * @param reserve_size Reserve up-front this many tasks for concurrent execution.  The engine
     *                     will also automatically grow this if needed.
     */
    engine(
        std::size_t reserve_size = 16
    )
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
    {
        struct epoll_event e{};
        e.events = EPOLLIN;

        e.data.ptr = m_submit_ptr;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_submit_fd, &e);

        m_background_thread = std::thread([this, reserve_size] { this->run(reserve_size); });
    }

    engine(const engine&) = delete;
    engine(engine&&) = delete;
    auto operator=(const engine&) -> engine& = delete;
    auto operator=(engine&&) -> engine& = delete;

    ~engine()
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
    }

    auto execute(coro::task<void>& task) -> bool
    {
        if(m_shutdown)
        {
            return false;
        }

        ++m_size;
        auto coro_handle = std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task.promise());

        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_submitted_tasks.emplace_back(coro_handle);
        }

        // Signal to the event loop there is a submitted task.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));

        return true;
    }

    auto poll(fd_type fd, poll_op op) -> coro::task<void>
    {
        co_await yield(
            [&](std::coroutine_handle<> handle)
            {
                struct epoll_event e{};
                e.events = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET;
                e.data.ptr = handle.address();
                epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &e);
            },
            [&]()
            {
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            }
        );
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
     * Immediately yields the current task and provides the coroutine handle that the user should
     * call `engine.resume(handle)` with via the functor_before parameter.
     * Normal usage of this might look like:
     *      engine.yield([&](std::coroutine_handle<> handle) {
     *          auto on_service_complete = [&]() { engine.resume(handle); };
     *          service.execute(on_service_complete);
     *      });
     * The above example will yield the current task and then through the 3rd party service's
     * on complete callback function let the engine know that it should resume execution of the task.
     *
     * This function along with `engine::resume()` are special additions for working with 3rd party
     * services that do not provide coroutine support, or that are event driven and cannot work
     * directly with the engine.
     * @tparam func Functor to invoke with the yielded coroutine handle to be resumed.
     * @param f Immediately invoked functor with the yield point coroutine handle to resume with.
     * @return A task to co_await until the manual `engine::resume(handle)` is called.
     */
    template<std::invocable<std::coroutine_handle<>> functor_before>
    auto yield(functor_before before) -> coro::task<void>
    {
        auto task = yield();
        auto coro_handle = std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task.promise());
        before(coro_handle);
        co_await task;
        co_return;
    }

    template<std::invocable<std::coroutine_handle<>> functor_before, std::invocable functor_after>
    auto yield(functor_before before, functor_after after) -> coro::task<void>
    {
        auto task = yield();
        auto coro_handle = std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task.promise());
        before(coro_handle);
        co_await task;
        after();
        co_return;
    }

    /**
     * Creates a yield point that can later be resumed by another thread.
     * @return A reference to the task to `co_await yield` on and the task's ID to call
     *         `engine.resume(handle)` from another thread to resume execution at this yield
     *         point.
     */
    auto yield() -> coro::task<void>
    {
        return []() -> coro::task<void>
        {
            co_await std::suspend_always{};
            co_return;
        }();
    }

    /**
     * Resumes a yield'ed task manually.  The use case is to first call `engine.yield()` and
     * co_await the manual yield point to pause execution of that task.  Then later on another
     * thread, probably a 3rd party service, call `engine.resume(handle)` to resume execution of
     * the task that was previously yield'ed with the 3rd party services result.
     * @param handle The task to resume its execution from its current yield point.
     */
    auto resume(std::coroutine_handle<> handle) -> void
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_resume_tasks.emplace_back(handle);
        }

        // Signal to the event loop there is a task to resume.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));
    }

    /**
     * Yields the current coroutine for `amount` of time.
     * @throw std::runtime_error If the internal system failed to setup required resources to wait.
     * @param amount The amount of time to wait.
     */
    auto wait(std::chrono::milliseconds amount) -> coro::task<void>
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
     * @return True if there are no tasks executing or waiting to be executed in this engine.
     */
    auto empty() const -> bool { return m_size == 0; }

    /**
     * @return True if this engine is currently running.
     */
    auto is_running() const noexcept -> bool { return m_is_running; }

    /**
     * @return True if this engine has been requested to shutdown.
     */
    auto is_shutdown() const noexcept -> bool { return m_shutdown; }

    /**
     * Requests the engine to finish processing all of its current tasks and shutdown.
     * New tasks submitted via `engine::execute()` will be rejected after this is called.
     * @param wait_for_tasks This call will block until all tasks are complete if shutdown_type::sync
     *                       is passed in, if shutdown_type::async is passed this function will tell
     *                       the engine to shutdown but not wait for all tasks to complete, it returns
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
     * @return A unique id to identify this engine.
     */
    auto engine_id() const -> uint32_t { return m_engine_id; }

private:
    static std::atomic<uint32_t> m_engine_id_counter;
    const uint32_t m_engine_id{m_engine_id_counter++};

    fd_type m_epoll_fd{-1};
    fd_type m_submit_fd{-1};

    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_shutdown{false};
    std::thread m_background_thread;

    std::mutex m_mutex;
    std::vector<std::coroutine_handle<coro::task<void>::promise_type>> m_submitted_tasks{};
    std::vector<std::coroutine_handle<>> m_resume_tasks{};

    std::atomic<std::size_t> m_size{0};

    auto run(const std::size_t growth_size) -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        std::vector<std::optional<coro::task<void>>> finalize_tasks{};
        std::list<std::size_t> finalize_indexes{};
        std::vector<std::list<std::size_t>::iterator> delete_indexes{};

        auto grow = [&]() mutable -> void
        {
            std::size_t new_size = finalize_tasks.size() + growth_size;
            for(size_t i = finalize_tasks.size(); i < new_size; ++i)
            {
                finalize_indexes.emplace_back(i);
            }
            finalize_tasks.resize(new_size);
        };
        grow();
        auto free_index = finalize_indexes.begin();

        auto completed = [](
            std::vector<std::list<std::size_t>::iterator>& delete_indexes,
            std::list<std::size_t>::iterator pos
        ) -> coro::task<void>
        {
            // Mark this task for deletion, it cannot delete itself.
            delete_indexes.push_back(pos);
            co_return;
        };

        constexpr std::chrono::milliseconds timeout{1000};
        constexpr std::size_t max_events = 8;
        std::array<struct epoll_event, max_events> events{};

        // Execute until stopped or there are more tasks to complete.
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
                        (void)value; // discard, the read merely reset the eventfd counter in the kernel.

                        std::vector<std::coroutine_handle<coro::task<void>::promise_type>> submit_tasks{};
                        std::vector<std::coroutine_handle<>> resume_tasks{};
                        {
                            std::lock_guard<std::mutex> lock{m_mutex};
                            submit_tasks.swap(m_submitted_tasks);
                            resume_tasks.swap(m_resume_tasks);
                        }

                        for(std::coroutine_handle<coro::task<void>::promise_type>& handle : submit_tasks)
                        {
                            if(!handle.done())
                            {
                                if(std::next(free_index) == finalize_indexes.end())
                                {
                                    grow();
                                }

                                auto pos = free_index;
                                std::advance(free_index, 1);

                                // Wrap in finalizing task to subtract from m_size.
                                auto index = *pos;
                                finalize_tasks[index] = completed(delete_indexes, pos);
                                auto& task = finalize_tasks[index].value();

                                auto& promise = handle.promise();
                                promise.set_continuation(task.handle());

                                handle.resume();
                            }
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
                        auto handle = std::coroutine_handle<>::from_address(handle_ptr);
                        if(!handle.done())
                        {
                            handle.resume();
                        }
                    }
                }

                // delete everything marked as completed.
                if(!delete_indexes.empty())
                {
                    for(const auto& pos : delete_indexes)
                    {
                        std::size_t index = *pos;
                        finalize_tasks[index] = std::nullopt;
                        // Put the deleted position at the end of the free indexes list.
                        finalize_indexes.splice(finalize_indexes.end(), finalize_indexes, pos);
                    }
                    m_size -= delete_indexes.size();
                    delete_indexes.clear();
                }
            }
        }

        m_is_running = false;
    }
};

} // namespace coro


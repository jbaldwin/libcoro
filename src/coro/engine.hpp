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

enum class await_op
{
    read = EPOLLIN,
    write = EPOLLOUT,
    read_write = EPOLLIN | EPOLLOUT
};

class engine
{
public:
    using task_type = coro::task<void>;
    using message_type = uint8_t;
    using socket_type = int;

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
    engine()
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
    {
        struct epoll_event e{};
        e.events = EPOLLIN;

        e.data.ptr = m_submit_ptr;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_submit_fd, &e);

        m_background_thread = std::thread([this] { this->run(); });
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

    auto poll(socket_type socket, await_op op) -> coro::task<void>
    {
        co_await suspend(
            [&](std::coroutine_handle<> handle)
            {
                struct epoll_event e{};
                e.events = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET;
                e.data.ptr = handle.address();
                epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, socket, &e);
            },
            [&]()
            {
                epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, socket, nullptr);
            }
        );
    }

    auto read(socket_type socket, std::span<char> buffer) -> coro::task<ssize_t>
    {
        co_await poll(socket, await_op::read);
        co_return ::read(socket, buffer.data(), buffer.size());
    }

    auto write(socket_type socket, const std::span<const char> buffer) -> coro::task<ssize_t>
    {
        co_await poll(socket, await_op::write);
        co_return ::write(socket, buffer.data(), buffer.size());;
    }

    /**
     * Immediately suspends the current task and provides the coroutine handle that the user should
     * call `engine.resume(handle)` with via the functor_before parameter.
     * Normal usage of this might look like:
     *      engine.suspend([&](std::coroutine_handle<> handle) {
     *          auto on_service_complete = [&]() { engine.resume(handle); };
     *          service.execute(on_service_complete);
     *      });
     * The above example will suspend the current task and then through the 3rd party service's
     * on complete callback function let the engine know that it should resume execution of the task.
     *
     * This function along with `engine::resume()` are special additions for working with 3rd party
     * services that do not provide coroutine support, or that are event driven and cannot work
     * directly with the engine.
     * @tparam func Functor to invoke with the suspended coroutine handle to be resumed.
     * @param f Immediately invoked functor with the suspend point coroutine handle to resume with.
     * @return A task to co_await until the manual `engine::resume(handle)` is called.
     */
    template<std::invocable<std::coroutine_handle<>> functor_before>
    auto suspend(functor_before before) -> coro::task<void>
    {
        auto task = suspend_point();
        auto coro_handle = std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task.promise());
        before(coro_handle);
        co_await task;
        co_return;
    }

    template<std::invocable<std::coroutine_handle<>> functor_before, std::invocable functor_after>
    auto suspend(functor_before before, functor_after after) -> coro::task<void>
    {
        auto task = suspend_point();
        auto coro_handle = std::coroutine_handle<coro::task<void>::promise_type>::from_promise(task.promise());
        before(coro_handle);
        co_await task;
        after();
        co_return;
    }

    /**
     * Creates a suspend point that can later be resumed by another thread.
     * @return A reference to the task to `co_await suspend` on and the task's ID to call
     *         `engine.resume(task_id)` from another thread to resume execution at this suspend
     *         point.
     */
    auto suspend_point() -> coro::task<void>
    {
        return []() -> coro::task<void>
        {
            co_await std::suspend_always{};
            co_return;
        }();
    }

    /**
     * Resumes a suspended task manually.  The use case is to first call `engine.suspend()` and
     * co_await the manual suspension point to pause execution of that task.  Then later on another
     * thread, probably a 3rd party service, call `engine.resume(handle)` to resume execution of
     * the task that was previously paused with the 3rd party services result.
     * @param handle The task to resume its execution from its current suspend point.
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
     * @return The number of active tasks still executing and unprocessed submitted tasks.
     */
    auto size() const -> std::size_t
    {
        return m_size.load();
    }

    /**
     * @return True if there are no tasks executing or waiting to be executed in this engine.
     */
    auto empty() const -> bool
    {
        return m_size == 0;
    }

    auto is_running() const noexcept -> bool
    {
        return m_is_running;
    }

    auto is_shutdown() const noexcept -> bool
    {
        return m_shutdown;
    }

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

private:
    static std::atomic<uint32_t> m_engine_id_counter;
    const uint32_t m_engine_id{m_engine_id_counter++};

    socket_type m_epoll_fd{-1};
    socket_type m_submit_fd{-1};

    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_shutdown{false};
    std::thread m_background_thread;

    std::atomic<uint64_t> m_task_id_counter{0};

    mutable std::mutex m_mutex;
    std::vector<std::coroutine_handle<coro::task<void>::promise_type>> m_submitted_tasks{};
    std::vector<std::coroutine_handle<>> m_resume_tasks{};

    std::atomic<std::size_t> m_size{0};

    auto run() -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        constexpr std::size_t growth_size{256};

        std::vector<std::optional<coro::task<void>>> finalize_tasks{};
        std::list<std::size_t> finalize_indexes{};
        std::vector<std::list<std::size_t>::iterator> delete_indexes{};

        finalize_tasks.resize(growth_size);
        for(size_t i = 0; i < growth_size; ++i)
        {
            finalize_indexes.emplace_back(i);
        }
        auto free_index = finalize_indexes.begin();


        auto completed = [](
            std::vector<std::list<std::size_t>::iterator>& delete_indexes,
            std::list<std::size_t>::iterator pos
        ) mutable -> coro::task<void>
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
                                if(free_index == finalize_indexes.end())
                                {
                                    // Backtrack to keep position in middle of the list.
                                    std::advance(free_index, -1);
                                    std::size_t new_size = finalize_tasks.size() + growth_size;
                                    for(size_t i = finalize_tasks.size(); i < new_size; ++i)
                                    {
                                        finalize_indexes.emplace_back(i);
                                    }
                                    finalize_tasks.resize(new_size);
                                    // Move forward to the new free index.
                                    std::advance(free_index, 1);
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


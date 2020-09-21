#pragma once

// #include "coro/task.hpp"

#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <span>

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

template<typename return_type = void>
class engine_task;
class engine;
using engine_task_id_type = uint64_t;

namespace engine_detail
{

struct promise_base
{
    friend struct final_awaitable;
    struct final_awaitable
    {
        final_awaitable(promise_base* promise) : m_promise(promise)
        {

        }

        auto await_ready() const noexcept -> bool
        {
            // std::cerr << "engine_detail::promise_base::final_awaitable::await_ready() => false\n";
            return false;
        }

        template<typename promise_type>
        auto await_suspend(std::coroutine_handle<promise_type> coroutine) noexcept -> std::coroutine_handle<>;

        auto await_resume() noexcept -> void
        {
            // no-op
        }

        promise_base* m_promise{nullptr};
    };

    promise_base() noexcept = default;
    ~promise_base() = default;

    auto initial_suspend()
    {
        return std::suspend_always{};
    }

    auto final_suspend()
    {
        return final_awaitable{this};
    }

    auto unhandled_exception() -> void
    {
        m_exception_ptr = std::current_exception();
    }

    auto set_continuation(std::coroutine_handle<> continuation) noexcept -> void
    {
        m_continuation = continuation;
    }

    auto parent_engine(engine* e) -> void { m_engine = e; }
    auto parent_engine() const -> engine* { return m_engine; }

    auto task_id(engine_task_id_type task_id) -> void { m_task_id = task_id; }
    auto task_id() const -> engine_task_id_type { return m_task_id; }

protected:
    std::coroutine_handle<> m_continuation{nullptr};
    std::optional<std::exception_ptr> m_exception_ptr{std::nullopt};
    engine* m_engine{nullptr};
    engine_task_id_type m_task_id{0};
};

template<typename return_type>
struct promise final : public promise_base
{
    using task_type = engine_task<return_type>;
    using coro_handle = std::coroutine_handle<promise<return_type>>;

    promise() noexcept = default;
    ~promise() = default;

    auto get_return_object() noexcept -> task_type;

    auto return_value(return_type result) -> void
    {
        m_result = std::move(result);
    }

    auto result() const & -> const return_type&
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }

        return m_result;
    }

    auto result() && -> return_type&&
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }

        return std::move(m_result);
    }

private:
    return_type m_result;
};

template<>
struct promise<void> : public promise_base
{
    using task_type = engine_task<void>;
    using coro_handle = std::coroutine_handle<promise<void>>;

    promise() noexcept = default;
    ~promise() = default;

    auto get_return_object() noexcept -> task_type;

    auto return_void() -> void { }

    auto result() const -> void
    {
        if(this->m_exception_ptr.has_value())
        {
            std::rethrow_exception(this->m_exception_ptr.value());
        }
    }
};

} // namespace engine_detail

template<typename return_type>
class engine_task
{
public:
    using task_type = engine_task<return_type>;
    using promise_type = engine_detail::promise<return_type>;
    using coro_handle = std::coroutine_handle<promise_type>;

    struct awaitable_base
    {
        awaitable_base(std::coroutine_handle<promise_type> coroutine) noexcept
            :   m_coroutine(coroutine)
        {

        }

        auto await_ready() const noexcept -> bool
        {
            // std::cerr << "engine_task::awaitable::await_ready() => " << (!m_coroutine || m_coroutine.done()) << "\n";
            return !m_coroutine || m_coroutine.done();
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            // std::cerr << "engine_task::awaitable::await_suspend()\n";
            m_coroutine.promise().set_continuation(awaiting_coroutine);
            return m_coroutine;
        }

        std::coroutine_handle<promise_type> m_coroutine{nullptr};
    };

    engine_task() noexcept
        : m_coroutine(nullptr)
    {

    }

    engine_task(coro_handle handle)
        : m_coroutine(handle)
    {

    }
    engine_task(const engine_task&) = delete;
    engine_task(engine_task&& other) noexcept
        : m_coroutine(other.m_coroutine)
    {
        other.m_coroutine = nullptr;
    }

    auto operator=(const engine_task&) -> engine_task& = delete;
    auto operator=(engine_task&& other) noexcept -> engine_task&
    {
        if(std::addressof(other) != this)
        {
            if(m_coroutine != nullptr)
            {
                m_coroutine.destroy();
            }

            m_coroutine = other.m_coroutine;
            other.m_coroutine = nullptr;
        }

        return *this;
    }

    /**
     * @return True if the task is in its final suspend or if the task has been destroyed.
     */
    auto is_ready() const noexcept -> bool
    {
        return m_coroutine == nullptr || m_coroutine.done();
    }

    auto resume() -> bool
    {
        if(!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto final_resume() -> bool
    {
        if(m_coroutine != nullptr && m_coroutine.done())
        {
            m_coroutine.resume();
            return true;
        }
        return false;
    }

    auto destroy() -> bool
    {
        if(m_coroutine != nullptr)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    auto operator co_await() const noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() noexcept -> decltype(auto)
            {
                // std::cerr << "engine_task::awaitable::await_resume()\n";
                return this->m_coroutine.promise().result();
            }
        };

        return awaitable{m_coroutine};
    }

    auto promise() & -> promise_type& { return m_coroutine.promise(); }
    auto promise() const & -> const promise_type& { return m_coroutine.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_coroutine.promise()); }

private:
    coro_handle m_coroutine{nullptr};

};

namespace engine_detail
{

template<typename return_type>
inline auto promise<return_type>::get_return_object() noexcept -> engine_task<return_type>
{
    return engine_task<return_type>{coro_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> engine_task<>
{
    return engine_task<>{coro_handle::from_promise(*this)};
}

} // namespace engine_detail


} // namespace coro

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
    /// To destroy the root execute() tasks upon await_resume().
    friend engine_detail::promise_base::final_awaitable;

public:
    using task_type = engine_task<void>;
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
    static constexpr engine_task_id_type submit_id = 0xFFFFFFFFFFFFFFFF;

public:
    engine()
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
    {
        struct epoll_event e{};
        e.events = EPOLLIN;

        e.data.u64 = submit_id;
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

    auto execute(task_type task) -> engine_task_id_type
    {
        ++m_size;
        auto task_id = m_task_id_counter++;
        auto& promise = task.promise();
        promise.parent_engine(this);
        promise.task_id(task_id);

        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_submitted_tasks.emplace_back(task_id, std::move(task));
        }

        // Signal to the event loop there is a submitted task.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));

        return task_id;
    }

    auto poll(socket_type socket, await_op op) -> engine_task<void>
    {
        // co_await suspend(
        //     [&](auto task_id)
        //     {
        //         struct epoll_event e{};
        //         e.events = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET;
        //         e.data.u64 = task_id;
        //         epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, socket, &e);
        //     },
        //     [&]()
        //     {
        //         epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, socket, nullptr);
        //     }
        // );

        auto [suspend_task, task_id] = suspend_point();

        struct epoll_event e{};
        e.events = static_cast<uint32_t>(op) | EPOLLONESHOT | EPOLLET;
        e.data.u64 = task_id;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, socket, &e);
        co_await suspend_task;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, socket, nullptr);
        co_return;
    }

    auto read(socket_type socket, std::span<char> buffer) -> engine_task<ssize_t>
    {
        co_await poll(socket, await_op::read);
        co_return ::read(socket, buffer.data(), buffer.size());
    }

    auto write(socket_type socket, const std::span<const char> buffer) -> coro::engine_task<ssize_t>
    {
        co_await poll(socket, await_op::write);
        co_return ::write(socket, buffer.data(), buffer.size());;
    }

    /**
     * Immediately suspends the current task and provides the task_id that the user should call
     * `engine.resume(task_id)` with via the functor parameter.  Normal usage of this might look
     * like:
     *      engine.suspend([&](auto task_id) {
     *          auto on_service_complete = [&]() { engine.resume(task_id); };
     *          service.execute(on_service_complete);
     *      });
     * The above example will suspend the current task and then through the 3rd party service's
     * on complete callback function let the engine know that it should resum execution of the task.
     *
     * This function along with `engine::resume()` are special additions for working with 3rd party
     * services that do not provide coroutine support, or that are event driven and cannot work
     * directly with the engine.
     * @tparam func Functor to invoke with the suspended task_id.
     * @param f Immediately invoked functor with the suspend point task_id to resume with.
     * @return A reference to the task to `co_await suspend` on and the task's ID to call
     *         `engine.resume(task_id)` from another thread to resume execution at this suspend
     *         point.
     */
    template<std::invocable<engine_task_id_type> functor_before>
    auto suspend(functor_before before) -> engine_task<void>&
    {
        auto [suspend_task, task_id] = suspend_point();
        before(task_id);
        return suspend_task;
    }

    template<std::invocable<engine_task_id_type> functor_before, std::invocable functor_after>
    auto suspend(functor_before before, functor_after after) -> engine_task<void>
    {
        auto [suspend_task, task_id] = suspend_point();
        before(task_id);
        co_await suspend_task;
        after();
        co_return;
    }

    /**
     * Creates a suspend point that can later be resumed by another thread.
     * @return A reference to the task to `co_await suspend` on and the task's ID to call
     *         `engine.resume(task_id)` from another thread to resume execution at this suspend
     *         point.
     */
    auto suspend_point() -> std::pair<engine_task<void>&, engine_task_id_type>
    {
        auto await_task_id = m_task_id_counter++;
        auto await_task = [&]() -> engine_task<void>
        {
            co_await std::suspend_always{};
            co_return;
        }();

        ++m_size;
        auto emplaced = m_active_tasks.emplace(await_task_id, std::move(await_task));
        auto& iter = emplaced.first;
        auto& task = iter->second;

        return {task, await_task_id};
    }

    /**
     * Resumes a suspended task manually.  The use case is to first call `engine.suspend()` and
     * co_await the manual suspension point to pause execution of that task.  Then later on another
     * thread, probably a 3rd party service, call `engine.resume(task_id)` to resume execution of
     * the task that was previously paused with the 3rd party services result.
     * @param task_id The task to resume its execution from its current suspend point.
     */
    auto resume(engine_task_id_type task_id) -> void
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_resume_tasks.emplace_back(task_id);
        }

        // Signal to the event loop there is a resume task.
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
    std::vector<std::pair<engine_task_id_type, task_type>> m_submitted_tasks;
    std::vector<engine_task_id_type> m_destroy_tasks;
    std::vector<engine_task_id_type> m_resume_tasks;
    std::map<engine_task_id_type, task_type> m_active_tasks;

    std::atomic<std::size_t> m_size{0};

    auto task_destroy(std::map<engine_task_id_type, task_type>::iterator iter) -> void
    {
        m_active_tasks.erase(iter);
        --m_size;
    }

    auto task_start(engine_task_id_type task_id, task_type& task) -> void
    {
        // std::cerr << "engine: submit task.resume() task_id=" << task_id << "\n";
        task.resume();

        // If the task is still awaiting then immediately remove.
        if(!task.is_ready())
        {
            m_active_tasks.emplace(task_id, std::move(task));
        }
        else
        {
            --m_size;
        }
    }

    auto register_destroy(engine_task_id_type id) -> void
    {
        m_destroy_tasks.emplace_back(id);

        // Signal to the event loop there is a task to possibly complete.
        uint64_t value{1};
        ::write(m_submit_fd, &value, sizeof(value));
    }

    auto task_register_destroy(engine_task_id_type task_id) -> void
    {
        auto task_found = m_active_tasks.find(task_id);
        if(task_found != m_active_tasks.end())
        {
            auto& task = task_found->second;
            if(task.is_ready())
            {
                task_destroy(task_found);
            }
        }
    }

    auto task_resume(engine_task_id_type task_id) -> void
    {
        auto task_found = m_active_tasks.find(task_id);
        if(task_found != m_active_tasks.end())
        {
            auto& task = task_found->second;
            if(task.is_ready())
            {
                task_destroy(task_found);
            }
            else
            {
                task.resume();
                if(task.is_ready())
                {
                    task_destroy(task_found);
                }
                // else suspended again
            }
        }
        else
        {
            std::cerr << "engine: task was not found task_id=" << task_id << "\n";
        }
    }

    auto run() -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        constexpr std::chrono::milliseconds timeout{1000};
        constexpr std::size_t max_events = 1;
        std::array<struct epoll_event, max_events> events{};

        // Execute until stopped or there are more tasks to complete.
        while(!m_shutdown || m_size > 0)
        {
            auto event_count = epoll_wait(m_epoll_fd, events.data(), max_events, timeout.count());
            if(event_count > 0)
            {
                for(std::size_t i = 0; i < event_count; ++i)
                {
                    engine_task_id_type task_id = events[i].data.u64;

                    if(task_id == submit_id)
                    {
                        uint64_t value{0};
                        ::read(m_submit_fd, &value, sizeof(value));
                        (void)value; // discard, the read merely reset the eventfd counter in the kernel.

                        std::vector<std::pair<engine_task_id_type, task_type>> submit_tasks;
                        std::vector<engine_task_id_type> destroy_tasks;
                        std::vector<engine_task_id_type> resume_tasks;
                        {
                            std::lock_guard<std::mutex> lock{m_mutex};
                            submit_tasks.swap(m_submitted_tasks);
                            destroy_tasks.swap(m_destroy_tasks);
                            resume_tasks.swap(m_resume_tasks);
                        }

                        // New tasks to start executing.
                        for(auto& [task_id, task] : submit_tasks)
                        {
                            task_start(task_id, task);
                        }

                        // Completed execute() root tasks, destroy them.
                        for(auto& task_id : destroy_tasks)
                        {
                            task_register_destroy(task_id);
                        }

                        // User code driven tasks to resume.
                        for(auto& task_id : resume_tasks)
                        {
                            task_resume(task_id);
                        }
                    }
                    else
                    {
                        // Individual poll task wake-up.
                        task_resume(task_id);
                    }
                }
            }
        }

        m_is_running = false;
    }
};

namespace engine_detail
{

template<typename promise_type>
inline auto promise_base::final_awaitable::await_suspend(std::coroutine_handle<promise_type> coroutine) noexcept -> std::coroutine_handle<>
{
    // If there is a continuation call it, otherwise this is the end of the line.
    auto& promise = coroutine.promise();
    if(promise.m_continuation != nullptr)
    {
        return promise.m_continuation;
    }
    else
    {
        // If this is a root submitted task check to see if its completely done.
        if(m_promise->m_engine != nullptr)
        {
            // std::cerr << "engine_detail::promise_base::final_awaitable::await_suspend() register_destroy(" << m_promise->m_task_id << ")\n";
            m_promise->m_engine->register_destroy(m_promise->m_task_id);
        }
        else
        {
            // std::cerr << "engine_detail::promise_base::final_awaitable::await_suspend() no engine ptr\n";
        }
        return std::noop_coroutine();
    }
}

} // namespace engine_detail

} // namespace coro


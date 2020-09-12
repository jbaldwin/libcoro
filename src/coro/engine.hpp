#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
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
    using task_id_type = uint64_t;
    using socket_type = int;

private:
    static constexpr task_id_type submit_id = 0xFFFFFFFFFFFFFFFF;

public:
    engine()
        :   m_epoll_fd(epoll_create1(EPOLL_CLOEXEC)),
            m_submit_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
    {
        struct epoll_event e{};
        e.events = EPOLLIN | EPOLLET;

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
        stop();
        m_background_thread.join();
        if(m_epoll_fd != -1)
        {
            close(m_epoll_fd);
            m_epoll_fd = -1;
        }
    }

    auto submit_task(task_type t) -> task_id_type
    {
        auto task_id = m_task_id_counter++;
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_submit_queued_tasks.emplace_back(task_id, std::move(t));
        }

        // Signal to the event loop there is a submitted task.
        uint64_t value{1};
        write(m_submit_fd, &value, sizeof(value));

        return task_id;
    }

    auto await(task_id_type id, socket_type socket, await_op op) -> void
    {
        struct epoll_event e{};
        e.events = static_cast<uint32_t>(op) | EPOLLONESHOT;
        e.data.u64 = id;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, socket, &e);
    }

    /**
     * @return The number of active tasks still executing.
     */
    auto size() const -> std::size_t
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_submit_queued_tasks.size();
    }

    auto is_running() const noexcept -> bool
    {
        return m_is_running;
    }

    auto stop() -> void
    {
        m_stop = true;
    }

private:
    static std::atomic<uint32_t> m_engine_id_counter;
    const uint32_t m_engine_id{m_engine_id_counter++};

    socket_type m_epoll_fd{-1};
    socket_type m_submit_fd{-1};

    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_stop{false};
    std::thread m_background_thread;

    std::atomic<uint64_t> m_task_id_counter{0};

    mutable std::mutex m_mutex;
    std::vector<std::pair<task_id_type, task_type>> m_submit_queued_tasks;
    std::map<task_id_type, task_type> m_active_tasks;

    auto run() -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        constexpr std::chrono::milliseconds timeout{1000};
        constexpr std::size_t max_events = 8;
        std::array<struct epoll_event, max_events> events;

        while(!m_stop)
        {
            auto event_count = epoll_wait(m_epoll_fd, events.data(), max_events, timeout.count());

            for(std::size_t i = 0; i < event_count; ++i)
            {
                task_id_type task_id = events[i].data.u64;

                if(task_id == submit_id)
                {
                    uint64_t value{0};
                    read(m_submit_fd, &value, sizeof(value));
                    (void)value; // discard, the read merely reset the eventfd counter in the kernel.

                    std::vector<std::pair<task_id_type, task_type>> grabbed_tasks;
                    {
                        std::lock_guard<std::mutex> lock{m_mutex};
                        grabbed_tasks.swap(m_submit_queued_tasks);
                    }

                    for(auto& [task_id, task] : grabbed_tasks)
                    {
                        std::cerr << "submit: task.resume()\n";
                        task.resume();

                        // If the task is still awaiting then push into active tasks.
                        if(!task.is_ready())
                        {
                            m_active_tasks.emplace(task_id, std::move(task));
                        }
                    }
                }
                else
                {
                    auto task_found = m_active_tasks.find(task_id);
                    if(task_found != m_active_tasks.end())
                    {
                        auto& task = task_found->second;
                        std::cerr << "await: task.resume()\n";
                        task.resume();

                        // If the task is done, remove it from internal state.
                        if(task.is_ready())
                        {
                            m_active_tasks.erase(task_found);
                        }
                    }
                    else
                    {
                        std::cerr << "await: not found\n";
                    }
                    // else unknown task, let the event just pass.
                }
            }
        }

        m_is_running = false;
    }
};

} // namespace coro


#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <zmq.hpp>

#include <iostream>

namespace coro
{

class engine
{
public:
    /// Always suspend at the start since the engine will call the first `resume()`.
    using task_type = coro::task<void>;
    using message_type = uint8_t;

    engine()
        :
            m_async_recv_events_socket(m_zmq_context, zmq::socket_type::pull),
            m_async_send_notify_socket(m_zmq_context, zmq::socket_type::push)
    {
        auto dsn = "inproc://engine";
        int linger = 125;
        uint32_t high_water_mark = 0;

        m_async_recv_events_socket.setsockopt<int>(ZMQ_LINGER, linger);
        m_async_recv_events_socket.setsockopt<uint32_t>(ZMQ_RCVHWM, high_water_mark);
        m_async_recv_events_socket.setsockopt<uint32_t>(ZMQ_SNDHWM, high_water_mark);

        m_async_send_notify_socket.setsockopt<int>(ZMQ_LINGER, linger);
        m_async_send_notify_socket.setsockopt<uint32_t>(ZMQ_RCVHWM, high_water_mark);
        m_async_send_notify_socket.setsockopt<uint32_t>(ZMQ_SNDHWM, high_water_mark);

        m_async_recv_events_socket.bind(dsn);
        m_async_send_notify_socket.connect(dsn);

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
    }

    auto submit_task(task_type t) -> bool
    {
        {
            std::lock_guard<std::mutex> lock{m_queued_tasks_mutex};
            m_queued_tasks.push_back(std::move(t));
        }

        message_type msg = 1;
        zmq::message_t zmq_msg{&msg, sizeof(msg)};

        zmq::send_result_t result;
        {
            std::lock_guard<std::mutex> lock{m_async_send_notify_mutex};
            result = m_async_send_notify_socket.send(zmq_msg, zmq::send_flags::none);
        }

        if(!result.has_value())
        {
            return false;
        }
        else
        {
            return result.value() == sizeof(msg);
        }
    }

    auto is_running() const noexcept -> bool
    {
        return m_is_running;
    }

    auto stop() -> void
    {
        m_stop = true;
    }

    auto run() -> void
    {
        using namespace std::chrono_literals;

        m_is_running = true;

        std::vector<zmq::pollitem_t> poll_items {
            zmq::pollitem_t{static_cast<void*>(m_async_recv_events_socket), 0, ZMQ_POLLIN, 0}
        };

        while(!m_stop)
        {
            auto events = zmq::poll(poll_items, 1000ms);

            if(events > 0)
            {
                while(true)
                {
                    message_type msg;
                    zmq::mutable_buffer buffer(static_cast<void*>(&msg), sizeof(msg));
                    auto result = m_async_recv_events_socket.recv(buffer, zmq::recv_flags::dontwait);

                    if(!result.has_value())
                    {
                        break; // while(true)
                    }
                    else if(result.value().truncated())
                    {
                        // let the task die? malformed message
                    }
                    else
                    {
                        std::vector<task_type> grabbed_tasks;
                        {
                            std::lock_guard<std::mutex> lock{m_queued_tasks_mutex};
                            grabbed_tasks.swap(m_queued_tasks);
                        }

                        for(auto& t : grabbed_tasks)
                        {
                            t.resume();

                            // if the task is still awaiting then push into active tasks.
                            if(!t.is_ready())
                            {
                                m_active_tasks.push_back(std::move(t));
                            }
                        }
                        m_active_tasks_count = m_active_tasks.size();
                    }
                }
            }
        }

        m_is_running = false;
    }

    /**
     * @return The number of active tasks still executing.
     */
    auto size() const -> std::size_t { return m_active_tasks_count; }

private:
    static std::atomic<uint32_t> m_engine_id_counter;
    const uint32_t m_engine_id{m_engine_id_counter++};

    zmq::context_t m_zmq_context{0};
    zmq::socket_t  m_async_recv_events_socket;
    std::mutex     m_async_send_notify_mutex{};
    zmq::socket_t  m_async_send_notify_socket;

    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_stop{false};
    std::thread m_background_thread;

    std::mutex m_queued_tasks_mutex;
    std::vector<task_type> m_queued_tasks;
    std::vector<task_type> m_active_tasks;

    std::atomic<std::size_t> m_active_tasks_count{0};
};

} // namespace coro


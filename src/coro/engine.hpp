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

// class message
// {
// public:
//     enum class type
//     {
//         new_web_request,
//         async_resume
//     };

//     message() = default;
//     message(type t, int socket)
//         : m_type(t),
//           m_socket(socket)
//     {

//     }
//     ~message() = default;

//     type m_type;
//     int m_socket;
// };

// class web_request
// {
// public:
//     web_request() = default;

//     web_request(int socket) : m_socket(socket)
//     {

//     }

//     ~web_request() = default;
// private:
//     int m_socket{0};
// };

class engine
{
public:
    /// Always suspend at the start since the engine will call the first `resume()`.
    using task = coro::task<void, std::suspend_always>;
    using message = uint8_t;

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

    auto submit_task(std::unique_ptr<task> t) -> bool
    {
        {
            std::lock_guard<std::mutex> lock{m_queued_tasks_mutex};
            m_queued_tasks.push_back(std::move(t));
        }

        message msg = 1;
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
        std::cerr << "running\n";

        std::vector<zmq::pollitem_t> poll_items {
            zmq::pollitem_t{static_cast<void*>(m_async_recv_events_socket), 0, ZMQ_POLLIN, 0}
        };

        while(!m_stop)
        {
            std::cerr << "polling\n";
            auto events = zmq::poll(poll_items, 1000ms);

            if(events > 0)
            {
                while(true)
                {
                    message msg;
                    zmq::mutable_buffer buffer(static_cast<void*>(&msg), sizeof(message));
                    auto result = m_async_recv_events_socket.recv(buffer, zmq::recv_flags::dontwait);

                    if(!result.has_value())
                    {
                        std::cerr << "result no value\n";
                        // zmq returns 0 on no messages available
                        break; // while(true)
                    }
                    else if(result.value().truncated())
                    {
                        std::cerr << "message received with incorrect size " << result.value().size << "\n";
                    }
                    else
                    {
                        std::cerr << "message received\n";

                        std::vector<std::unique_ptr<task>> grabbed_tasks;
                        {
                            std::lock_guard<std::mutex> lock{m_queued_tasks_mutex};
                            grabbed_tasks.swap(m_queued_tasks);
                        }

                        for(auto& t : grabbed_tasks)
                        {
                            // start executing now
                            t->resume();

                            // if the task is awaiting then push into active tasks.
                            if(!t->is_done())
                            {
                                m_active_tasks.push_back(std::move(t));
                            }
                        }
                    }
                }
            }
        }

        m_is_running = false;
        std::cerr << "stopping\n";
    }

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
    std::vector<std::unique_ptr<task>> m_queued_tasks;
    std::vector<std::unique_ptr<task>> m_active_tasks;
};

} // namespace coro


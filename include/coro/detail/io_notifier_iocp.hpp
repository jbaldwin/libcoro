#pragma once
#include <mutex>
#include "coro/detail/poll_info.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"
#include "coro/signal.hpp"

namespace coro::detail
{
class timer_handle;

class io_notifier_iocp
{
public:
    enum class completion_key : unsigned long long
    {
        signal,
        socket,
        timer
    };

public:
    io_notifier_iocp();

    io_notifier_iocp(const io_notifier_iocp&)                      = delete;
    io_notifier_iocp(io_notifier_iocp&&)                           = delete;
    auto operator=(const io_notifier_iocp&) -> io_notifier_iocp&   = delete;
    auto operator=(io_notifier_iocp&&) -> io_notifier_iocp&        = delete;

    ~io_notifier_iocp();

    auto watch_timer(const detail::timer_handle& timer, std::chrono::nanoseconds duration) -> bool;

    auto watch(const coro::signal& signal, void* data) -> bool;

    auto unwatch_timer(const detail::timer_handle& timer) -> bool;

    auto next_events(
        std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events, 
        std::chrono::milliseconds timeout
    ) -> void;

    //static auto event_to_poll_status(const event_t& event) -> poll_status;

    auto iocp() const noexcept -> void* { return m_iocp; }

private:
    void* m_iocp{};

    void set_signal_active(void* data, bool active);
    void process_active_signals(
        std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events
    );

    std::mutex         m_active_signals_mutex;
    std::vector<void*> m_active_signals;
};
}
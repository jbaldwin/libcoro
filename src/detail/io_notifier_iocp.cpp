#include "coro/detail/io_notifier_iocp.hpp"
#include <Windows.h>
#include <iostream>
#include "coro/detail/signal_win32.hpp"
#include "coro/detail/timer_handle.hpp"
#include <coro/detail/iocp_overlapped.hpp>

namespace coro::detail
{
struct signal_win32::Event
{
    void*      data;
    bool       is_set;
};

io_notifier_iocp::io_notifier_iocp()
{
    std::size_t concurrent_threads = 0; // TODO

    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, concurrent_threads);
}

io_notifier_iocp::~io_notifier_iocp()
{
    CloseHandle(m_iocp);
}

static VOID CALLBACK onTimerFired(LPVOID timerPtr, DWORD, DWORD)
{
    auto* handle = static_cast<detail::timer_handle*>(timerPtr);
    
    // Completion key 3 means timer
    PostQueuedCompletionStatus(handle->get_iocp(), 0, 3, static_cast<LPOVERLAPPED>(const_cast<void*>(handle->get_inner())));
}

auto io_notifier_iocp::watch_timer(const detail::timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
    if (timer.m_iocp == nullptr) {
        timer.m_iocp = m_iocp;
    }
    else if (timer.m_iocp != m_iocp) 
    {
        throw std::runtime_error("Timer is already associated with a different IOCP handle. Cannot reassign.");
    }

    LARGE_INTEGER dueTime{};
    // time in 100ns intervals, negative for relative
    dueTime.QuadPart = -duration.count() / 100;

    // `timer_handle` must remain alive until the timer fires.
    // This is guaranteed by `io_scheduler`, which owns the timer lifetime.
    //
    // We could allocate a separate `timer_context` on the heap to decouple ownership,
    // but safely freeing it is difficult without introducing overhead or leaks:
    // the timer can be cancelled, and we have no guaranteed way to retrieve the pointer.
    //
    // Therefore, we directly pass a pointer to `timer_handle` as the APC context.
    // This avoids allocations and should be safe (I hope) under our scheduler's lifetime guarantees.
    return SetWaitableTimer(timer.get_native_handle(), &dueTime, 0, &onTimerFired, (void*)std::addressof(timer), FALSE);
}

auto io_notifier_iocp::unwatch_timer(const detail::timer_handle& timer) -> bool
{
    return CancelWaitableTimer(timer.get_native_handle());
}

auto io_notifier_iocp::watch(const coro::signal& signal, void* data) -> bool
{
    signal.m_iocp = m_iocp;
    signal.m_data = data;
    return true;
}

auto io_notifier_iocp::next_events(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events, 
    const std::chrono::milliseconds timeout
) -> void
{
    DWORD        bytes_transferred = 0;
    completion_key completion_key;
    LPOVERLAPPED overlapped       = nullptr;
    DWORD        timeoutMs        = (timeout.count() == -1) ? INFINITE : static_cast<DWORD>(timeout.count());

    process_active_signals(ready_events);

    while (true)
    {
        BOOL success = GetQueuedCompletionStatus(
            m_iocp, &bytes_transferred, reinterpret_cast<PULONG_PTR>(std::addressof(completion_key)), &overlapped, timeoutMs);

        if (!success && overlapped == nullptr)
        {
            // Timeout or critical error
            return;
        }

        switch (completion_key)
        {
            case completion_key::signal:
            {
                if (!overlapped)
                    continue;

                auto* event = reinterpret_cast<signal_win32::Event*>(overlapped);
                set_signal_active(event->data, event->is_set);
                if (event->is_set)
                {
                    // poll_status doesn't matter.
                    ready_events.emplace_back(event->data, coro::poll_status::event);
                }
                continue;
            }
            case completion_key::socket:
            {
                auto* info = reinterpret_cast<overlapped_io_operation*>(overlapped);
                if (!info)
                    continue;

                info->bytes_transferred = bytes_transferred;
                
                coro::poll_status status;

                if (!success)
                {
                    DWORD err = GetLastError();
                    if (err == ERROR_NETNAME_DELETED || err == ERROR_CONNECTION_ABORTED ||
                        err == ERROR_OPERATION_ABORTED)
                        status = coro::poll_status::closed;
                    else
                        status = coro::poll_status::error;
                }
                else if (bytes_transferred == 0)
                {
                    // The connection is closed normally
                    status = coro::poll_status::closed;
                }
                else
                {
                    status = coro::poll_status::event;
                }

                ready_events.emplace_back(&info->pi, status);
                continue;
            }
            case completion_key::timer: // timer
            {
                // Remember that it's not real poll_info, it's just a pointer to some random data
                // io_scheduler must handle it.
                // poll_status doesn't matter.
                auto* handle_ptr = reinterpret_cast<detail::poll_info*>(overlapped);
                ready_events.emplace_back(handle_ptr, coro::poll_status::event);
                break;
            }
            default:
            {
                throw std::runtime_error("Received unknown completion key.");
            }
        }

    }
}
void io_notifier_iocp::set_signal_active(void* data, bool active)
{
    std::scoped_lock lk{m_active_signals_mutex};
    if (active) {
        m_active_signals.emplace_back(data);
    }
    else {
        m_active_signals.erase(std::remove(std::begin(m_active_signals), std::end(m_active_signals), data));
    }
}
void io_notifier_iocp::process_active_signals(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events
)
{
    for (void* data : m_active_signals)
    {
        // poll_status doesn't matter.
        ready_events.emplace_back(data, poll_status::event);
    }
}
}
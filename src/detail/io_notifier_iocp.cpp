#include "coro/detail/io_notifier_iocp.hpp"
#include <Windows.h>
#include "coro/detail/signal_win32.hpp"

namespace coro::detail
{
struct signal_win32::Event
{
    OVERLAPPED overlapped;
    void*      data;
    bool       is_set;
};


io_notifier_iocp::io_notifier_iocp()
{
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

io_notifier_iocp::~io_notifier_iocp()
{
    CloseHandle(m_iocp);
}

auto io_notifier_iocp::watch(const coro::signal& signal, void* data) -> bool
{
    signal.m_iocp = m_iocp;
    signal.m_data = data;
}

auto io_notifier_iocp::next_events(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events, 
    std::chrono::milliseconds timeout
) -> void
{
    DWORD        bytesTransferred = 0;
    ULONG_PTR    completionKey    = 0;
    LPOVERLAPPED overlapped       = nullptr;
    DWORD        timeoutMs        = (timeout.count() == -1) ? INFINITE : static_cast<DWORD>(timeout.count());

    while (true)
    {
        BOOL success = GetQueuedCompletionStatus(m_iocp, &bytesTransferred, &completionKey, &overlapped, timeoutMs);

        if (!success && overlapped == nullptr)
        {
            // Timeout or critical error
            return;
        }

        if (completionKey == signal_win32::signal_key)
        {
            if (!overlapped)
                continue;

            auto* event = reinterpret_cast<signal_win32::Event*>(overlapped);
            set_signal_active(event->data, event->is_set);
            continue;
        }

        if (completionKey == 2) // socket
        {
            auto* info = reinterpret_cast<detail::poll_info*>(completionKey);
            if (!info)
                continue;
            coro::poll_status status;

            if (!success) {
                DWORD err = GetLastError();
                if (err == ERROR_NETNAME_DELETED || err == ERROR_CONNECTION_ABORTED || err == ERROR_OPERATION_ABORTED)
                    status = coro::poll_status::closed;
                else
                    status = coro::poll_status::error;
            }
            else if (bytesTransferred == 0) {
                // The connection is closed normally
                status = coro::poll_status::closed;
            }
            else {
                status = coro::poll_status::event;
            }

            ready_events.emplace_back(info, status);
            continue;
        }

        throw std::runtime_error("Received unknown completion key.");
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
}
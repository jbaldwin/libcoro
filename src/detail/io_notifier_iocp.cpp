#include "coro/detail/io_notifier_iocp.hpp"
#include "coro/detail/signal_win32.hpp"
#include "coro/detail/timer_handle.hpp"
#include <Windows.h>
#include <coro/detail/iocp_overlapped.hpp>

namespace coro::detail
{
io_notifier_iocp::io_notifier_iocp()
{
    DWORD concurrent_threads = 0; // TODO

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
    PostQueuedCompletionStatus(
        handle->get_iocp(),
        0,
        static_cast<int>(io_notifier::completion_key::timer),
        static_cast<LPOVERLAPPED>(const_cast<void*>(handle->get_inner())));
}

auto io_notifier_iocp::watch_timer(const detail::timer_handle& timer, std::chrono::nanoseconds duration) -> bool
{
    if (timer.m_iocp == nullptr)
    {
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

/**
 * I think this cycle needs a little explanation.
 *
 *  == Completion keys ==
 *
 *  1. **Signals**
 *      IOCP is not like epoll or kqueue, it works only with file-related events.
 *      To emulate signals io_scheduler uses (previously a pipe, now abstracted into signals)
 *      I use an array that tracks all active signals and dispatches them on every call.
 *
 *      Because of this, we need a pointer to the IOCP handle inside `@ref coro::signal`.
 *
 *  2. **Sockets**
 *      It's nothing special. We just get the pointer to poll_info through `@ref coro::detail::overlapped_poll_info`.
 *      The overlapped structure is stored inside the coroutine, so as long as coroutine lives everything will be fine.
 *      But if the coroutine dies, it's UB. I see no point in using heap, since if we have no coroutine, what should
 *      we dispatch?
 *
 *      **Important**
 *      All sockets **must** have the following flags set using `SetFileCompletionNotificationModes`:
 *
 *      - `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`:
 *          Prevents IOCP from enqueuing completions if the operation completes synchronously.
 *          If disabled, IOCP might try to access an `OVERLAPPED` structure from a coroutine that has already died.
 *          This can cause undefined behavior if the coroutine is dead and its memory is invalid.
 *          If it's still alive - you got lucky.
 *
 *      - `FILE_SKIP_SET_EVENT_ON_HANDLE`:
 *          Prevents the system from setting a WinAPI event on the socket handle.
 *          We don’t use system events, so this is safe and gives a small performance boost.
 *
 *  3. Timers
 *       IOCP doesn’t support timers directly - Windows has no `timerfd` like Unix.
 *       We use waitable timers (see `timer_handle.cpp`) to emulate this.
 *       When the timer fires, it triggers `@ref onTimerFired`, which posts an event to the IOCP queue.
 *       Since it's our own event we don't have to pass a valid OVERLAPPED structure,
 *       we just pass a pointer to the timer data and then emplace it into `ready_events`.
 *
 *  **The cycle itself**
 *  Rewrite to GetQueuedCompletionStatusEx
 *
 */
auto io_notifier_iocp::next_events(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events,
    const std::chrono::milliseconds                                timeout,
    const size_t                                                   max_events) -> void
{
    using namespace std::chrono;

    auto handle = [&](const DWORD bytes, const completion_key key, const LPOVERLAPPED ov)
    {
        switch (key)
        {
            case completion_key::signal_set:
            case completion_key::signal_unset:
                if (ov)
                    set_signal_active(ov, key == completion_key::signal_set);
                break;
            case completion_key::socket:
                if (ov)
                {
                    auto* info              = reinterpret_cast<overlapped_io_operation*>(ov);
                    info->bytes_transferred = bytes;
                    coro::poll_status st =
                        (bytes == 0 && !info->is_accept) ? coro::poll_status::closed : coro::poll_status::event;
                    ready_events.emplace_back(&info->pi, st);
                }
                break;
            case completion_key::timer:
                if (ov)
                    ready_events.emplace_back(reinterpret_cast<detail::poll_info*>(ov), coro::poll_status::event);
                break;
            default:
                throw std::runtime_error("Unknown completion key");
        }
    };

    process_active_signals(ready_events);

    if (timeout.count() >= 0)
    {
        milliseconds remaining = timeout;
        while (remaining.count() > 0 && ready_events.size() < max_events)
        {
            auto           t0    = steady_clock::now();
            DWORD          bytes = 0;
            completion_key key{};
            LPOVERLAPPED   ov = nullptr;

            BOOL ok = GetQueuedCompletionStatus(
                m_iocp, &bytes, reinterpret_cast<PULONG_PTR>(&key), &ov, static_cast<DWORD>(remaining.count()));
            auto t1 = steady_clock::now();

            if (!ok && ov == nullptr)
            {
                break;
            }

            handle(bytes, key, ov);

            auto took = duration_cast<milliseconds>(t1 - t0);
            if (took < remaining)
                remaining -= took;
            else
                break;
        }
    }
    else
    {
        DWORD          bytes = 0;
        completion_key key{};
        LPOVERLAPPED   ov = nullptr;
        BOOL ok = GetQueuedCompletionStatus(m_iocp, &bytes, reinterpret_cast<PULONG_PTR>(&key), &ov, INFINITE);

        if (!ok && ov == nullptr)
        {
            return;
        }
        handle(bytes, key, ov);
    }

    while (ready_events.size() < max_events)
    {
        DWORD          bytes = 0;
        completion_key key{};
        LPOVERLAPPED   ov = nullptr;
        BOOL           ok = GetQueuedCompletionStatus(
            m_iocp,
            &bytes,
            reinterpret_cast<PULONG_PTR>(&key),
            &ov,
            0 // non-blocking
        );
        if (!ok && ov == nullptr)
            break;
        handle(bytes, key, ov);
    }
}

void io_notifier_iocp::set_signal_active(void* data, bool active)
{
    std::scoped_lock lk{m_active_signals_mutex};
    if (active)
    {
        m_active_signals.emplace_back(data);
    }
    else if (auto it = std::find(m_active_signals.begin(), m_active_signals.end(), data); it != m_active_signals.end())
    {
        // Fast erase
        std::swap(m_active_signals.back(), *it);
        m_active_signals.pop_back();
    }
}
void io_notifier_iocp::process_active_signals(
    std::vector<std::pair<detail::poll_info*, coro::poll_status>>& ready_events)
{
    for (void* data : m_active_signals)
    {
        // poll_status doesn't matter.
        ready_events.emplace_back(static_cast<poll_info*>(data), poll_status::event);
    }
}
} // namespace coro::detail
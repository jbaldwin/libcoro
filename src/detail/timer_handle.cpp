#include "coro/detail/timer_handle.hpp"

#include "coro/io_notifier.hpp"
#if defined(CORO_PLATFORM_WINDOWS)
    #include <Windows.h>
#endif

namespace coro::detail
{

#if defined(CORO_PLATFORM_BSD)

static auto kqueue_current_timer_fd = std::atomic<fd_t>{0};

timer_handle::timer_handle(const void* timer_handle_ptr, io_notifier& notifier)
    : m_native_handle{kqueue_current_timer_fd++},
      m_timer_handle_ptr(timer_handle_ptr)
{
    (void)notifier;
}
timer_handle::~timer_handle()
{
}

#elif defined(CORO_PLATFORM_LINUX)

timer_handle::timer_handle(const void* timer_handle_ptr, io_notifier& notifier)
    : m_native_handle(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      m_timer_handle_ptr(timer_handle_ptr)
{
    notifier.watch(m_native_handle, coro::poll_op::read, const_cast<void*>(m_timer_handle_ptr), true);
}
timer_handle::~timer_handle()
{
}

#elif defined(CORO_PLATFORM_WINDOWS)

timer_handle::timer_handle(const void* timer_handle_ptr, io_notifier& notifier)
    : m_native_handle(CreateWaitableTimerW(nullptr, FALSE, nullptr)),
      m_timer_handle_ptr(timer_handle_ptr)
{
    if (m_native_handle == nullptr)
    {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(), "Failed to CreateWaitableTimer");
    }
    (void)notifier;
}
timer_handle::~timer_handle()
{
    CloseHandle(m_native_handle);
}
#endif

} // namespace coro::detail

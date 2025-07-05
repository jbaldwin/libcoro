#pragma once

#include "coro/fd.hpp"
#include "coro/io_notifier.hpp"

namespace coro
{

namespace detail
{

class timer_handle
{
#if defined(CORO_PLATFORM_UNIX)
    using native_handle_t = coro::fd_t;
#elif defined(CORO_PLATFORM_WINDOWS)
    friend class io_notifier_iocp;
    using native_handle_t = void *;

    mutable void* m_iocp = nullptr;
#endif

    native_handle_t m_native_handle;
    const void* m_timer_handle_ptr = nullptr;

public:
    timer_handle(const void* timer_handle_ptr, io_notifier& notifier);
    ~timer_handle();

    native_handle_t get_native_handle() const { return m_native_handle; }

    const void* get_inner() const { return m_timer_handle_ptr; }

#if defined(CORO_PLATFORM_WINDOWS)
    void* get_iocp() const { return m_iocp; }
#endif
};

} // namespace detail

} // namespace coro

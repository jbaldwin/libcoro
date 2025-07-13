#pragma once

#include "coro/fd.hpp"
#include "coro/io_notifier.hpp"

namespace coro
{

namespace detail
{

#if defined(CORO_PLATFORM_UNIX)
class timer_handle
{
    using native_handle_t = coro::fd_t;

public:
    timer_handle(const void* timer_handle_ptr, io_notifier& notifier);
    ~timer_handle();

    native_handle_t get_native_handle() const { return m_native_handle; }

    const void* get_inner() const { return m_timer_handle_ptr; }

private:
    native_handle_t m_native_handle;
    const void*     m_timer_handle_ptr = nullptr;
};

#elif defined(CORO_PLATFORM_WINDOWS)
class timer_handle
{
    friend class io_notifier_iocp;
    using native_handle_t = void*;

public:
    timer_handle(const void* timer_handle_ptr, io_notifier& notifier);
    ~timer_handle();

    [[nodiscard]] native_handle_t get_native_handle() const { return m_native_handle; }

    [[nodiscard]] const void* get_inner() const { return m_timer_handle_ptr; }
    [[nodiscard]] void*       get_iocp() const { return m_iocp; }

private:
    native_handle_t m_native_handle;

    const void* m_timer_handle_ptr = nullptr;
    void*       m_iocp             = nullptr;
    void*       m_wait_handle      = nullptr;
};
#endif

} // namespace detail

} // namespace coro

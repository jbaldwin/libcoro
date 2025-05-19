#pragma once

#include "coro/fd.hpp"
#include "coro/io_notifier.hpp"

namespace coro
{

namespace detail
{

class timer_handle
{
    coro::fd_t  m_fd;
    const void* m_timer_handle_ptr = nullptr;

public:
    timer_handle(const void* timer_handle_ptr, io_notifier& notifier);

    coro::fd_t get_fd() const { return m_fd; }

    const void* get_inner() const { return m_timer_handle_ptr; }
};

} // namespace detail

} // namespace coro

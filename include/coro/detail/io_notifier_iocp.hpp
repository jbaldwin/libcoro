#pragma once

#include "coro/detail/win32_headers.hpp"
#include "coro/fd.hpp"
#include <chrono>

namespace coro::detail
{
class timer_handle;

class io_notifier_iocp
{
public:
    io_notifier_iocp() { m_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0); }

    ~io_notifier_iocp() { CloseHandle(m_handle); }

private:
    void* m_handle;
};
} // namespace coro::detail
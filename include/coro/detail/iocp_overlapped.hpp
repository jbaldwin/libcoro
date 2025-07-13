// NOTE: This file includes <Windows.h>, which pulls in many global symbols.
// Do not include this file from headers. Include only in implementation files (.cpp) or modules.

#pragma once
#include "coro/io_scheduler.hpp"
#include <coro/detail/poll_info.hpp>

// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2ipdef.h>
#include <MSWSock.h>
#include "coro/detail/iocp_overlapped.hpp"
// clang-format on

namespace coro::detail
{
struct overlapped_io_operation
{
    OVERLAPPED ov{}; // Base Windows OVERLAPPED structure for async I/O
    poll_info  pi;
    DWORD      bytes_transferred{}; // Number of bytes read or written once the operation completes

    SOCKET socket{};
};

template<typename status_enum, typename buffer_type, bool is_read, typename operation_fn>
    requires std::is_invocable_r_v<int, operation_fn, SOCKET, overlapped_io_operation&, WSABUF&>
auto perform_write_read_operation(
    const std::shared_ptr<io_scheduler>& scheduler,
    SOCKET                               socket,
    operation_fn&&                       operation,
    buffer_type                          buffer,
    std::chrono::milliseconds            timeout) -> task<std::pair<status_enum, buffer_type>>
{
    overlapped_io_operation ov{};
    WSABUF                  buf{};

    ov.socket = socket;

    buf.buf = const_cast<char*>(buffer.data());
    buf.len = buffer.size();

    auto get_result_buffer = [&]()
    {
        if constexpr (is_read)
            return ov.bytes_transferred == 0 ? buffer_type{} : buffer_type{buffer.data(), ov.bytes_transferred};
        else
            return ov.bytes_transferred == 0
                       ? buffer_type{}
                       : buffer_type{buffer.data() + ov.bytes_transferred, buffer.size() - ov.bytes_transferred};
    };

    auto r = operation(socket, std::ref(ov), std::ref(buf));

    // Operation has been completed synchronously, no need to wait for event.
    if (r == 0)
    {
        co_return {ov.bytes_transferred == 0 ? status_enum::closed : status_enum::ok, get_result_buffer()};
    }
    if (WSAGetLastError() != WSA_IO_PENDING)
    {
        co_return {status_enum::error, buffer};
    }

    // We need loop in case the operation completes right away with the timeout.
    // In this case we just co_await our poll_info once more to correct status.
    while (true)
    {
        switch (co_await scheduler->poll(ov.pi, timeout))
        {
            case poll_status::event:
                co_return {status_enum::ok, get_result_buffer()};
            case poll_status::timeout:
            {
                if (const BOOL success = CancelIoEx(reinterpret_cast<HANDLE>(socket), &ov.ov); !success)
                {
                    if (const auto err = GetLastError(); err == ERROR_NOT_FOUND)
                    {
                        // Operation has been completed, we need to co_await once more
                        timeout = {}; // No need in timeout
                        continue;
                    }
                }
                co_return {status_enum::timeout, get_result_buffer()};
            }
            case poll_status::closed:
                co_return {status_enum::closed, buffer};
            case poll_status::error:
            default:
                co_return {status_enum::error, buffer};
        }
    }
}

} // namespace coro::detail
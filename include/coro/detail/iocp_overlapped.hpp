// NOTE: This file includes <Windows.h>, which pulls in many global symbols.
// Do not include this file from headers. Include only in implementation files (.cpp) or modules.

#pragma once
#include <Windows.h>
#include <coro/detail/poll_info.hpp>

namespace coro::detail
{
struct overlapped_io_operation
{
    OVERLAPPED  ov{}; // Base Windows OVERLAPPED structure for async I/O
    poll_info   pi;
    std::size_t bytes_transferred{}; // Number of bytes read or written once the operation completes

    /**
     * Marks this operation as an AcceptEx call:
     * allows distinguishing accept events from normal reads/writes
     * and ensures that a zero-byte completion isn’t treated as poll_status::closed
     */
    bool is_accept{};
};

} // namespace coro::detail
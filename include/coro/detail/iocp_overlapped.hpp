// NOTE: This file includes <Windows.h>, which pulls in many global symbols.
// Do not include this file from headers. Include only in implementation files (.cpp) or modules.

#pragma once
#include <coro/detail/poll_info.hpp>
#include <Windows.h>
#include <WinSock2.h>

namespace coro::detail
{
struct overlapped_io_operation
{
    OVERLAPPED  ov{};
    poll_info   pi;
    std::size_t bytes_transferred{};
};
}
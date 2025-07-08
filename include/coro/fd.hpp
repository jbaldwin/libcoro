#pragma once
#include "platform.hpp"
#include <cstdint>

namespace coro
{
#if defined(CORO_PLATFORM_UNIX)
using fd_t = int;
#elif defined(CORO_PLATFORM_WINDOWS)
using fd_t = uint64_t *;
#endif

} // namespace coro

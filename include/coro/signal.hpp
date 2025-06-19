#pragma once
#include "coro/platform.hpp"

#if defined(CORO_PLATFORM_UNIX)
    #include "detail/signal_unix.hpp"
#elif defined(CORO_PLATFORM_WINDOWS)
    #include "detail/signal_win32.hpp"
#endif

namespace coro
{
#if defined(CORO_PLATFORM_UNIX)
    using signal = detail::signal_unix;
#elif defined(CORO_PLATFORM_WINDOWS)
    using signal = detail::signal_win32;
#endif
} // namespace coro
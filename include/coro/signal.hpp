#pragma once
#include "detail/signal_unix.hpp"

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__linux__)
    #include "detail/signal_unix.hpp"
#elif defined(_WIN32) || defined(_WIN64)
    #include "detail/signal_win32.hpp"
#endif

namespace coro
{
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__linux__)
    using signal = detail::signal_unix;
#elif defined(_WIN32) || defined(_WIN64)
    using signal = detail::signal_win32;
#endif
} // namespace coro
#pragma once
#include "coro/detail/platform.hpp"

#if defined(LIBCORO_PLATFORM_BSD)
    #include "coro/detail/io_notifier_kqueue.hpp"
#elif defined(LIBCORO_PLATFORM_LINUX)
    #include "coro/detail/io_notifier_epoll.hpp"
#elif defined(LIBCORO_PLATFORM_WINDOWS)
    #include "coro/detail/io_notifier_iocp.hpp"
#endif

namespace coro
{

#if defined(LIBCORO_PLATFORM_BSD)
using io_notifier = detail::io_notifier_kqueue;
#elif defined(LIBCORO_PLATFORM_LINUX)
using io_notifier = detail::io_notifier_epoll;
#elif defined(LIBCORO_PLATFORM_WINDOWS)
using io_notifier = detail::io_notifier_iocp;
#endif

} // namespace coro

#pragma once

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include "coro/detail/io_notifier_kqueue.hpp"
#elif defined(__linux__)
    #include "coro/detail/io_notifier_epoll.hpp"
#elif defined(_WIN32) || defined(_WIN64)
    #include "coro/detail/io_notifier_iocp.hpp"
#endif

namespace coro
{

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
using io_notifier = detail::io_notifier_kqueue;
#elif defined(__linux__)
using io_notifier = detail::io_notifier_epoll;
#elif defined(_WIN32) || defined(_WIN64)
    using io_notifier = detail::io_notifier_iocp;
#endif

} // namespace coro

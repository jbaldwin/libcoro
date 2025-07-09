#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/fd.hpp"
#include "coro/task.hpp"
#include "coro/platform.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/poll.hpp"
#if defined(CORO_PLATFORM_WINDOWS)
    #include "coro/detail/poll_info.hpp"
#endif
#endif // #ifdef LIBCORO_FEATURE_NETWORKING

#include <chrono>
#include <concepts>
#include <coroutine>
#include <utility>

namespace coro::concepts
{

// clang-format off
template<typename executor_type>
concept executor = requires(executor_type e, std::coroutine_handle<> c)
{
    { e.schedule() } -> coro::concepts::awaiter;
    { e.spawn(std::declval<coro::task<void>>()) } -> std::same_as<bool>;
    { e.yield() } -> coro::concepts::awaiter;
    { e.resume(c) } -> std::same_as<bool>;
    { e.size() } -> std::same_as<std::size_t>;
    { e.empty() } -> std::same_as<bool>;
    { e.shutdown() } -> std::same_as<void>;
};

#ifdef LIBCORO_FEATURE_NETWORKING
#if defined(CORO_PLATFORM_UNIX)
template<typename executor_type>
concept io_executor = executor<executor_type> and requires(executor_type e, std::coroutine_handle<> c, fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout)
{
    { e.poll(fd, op, timeout) } -> std::same_as<coro::task<poll_status>>;
};
#elif defined(CORO_PLATFORM_WINDOWS)
template<typename executor_type>
concept io_executor = executor<executor_type> and requires(executor_type e, coro::detail::poll_info pi, std::chrono::milliseconds timeout)
{
    { e.poll(pi, timeout) } -> std::same_as<coro::task<poll_status>>;
};
#endif
#endif // #ifdef LIBCORO_FEATURE_NETWORKING

// clang-format on

} // namespace coro::concepts

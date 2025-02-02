#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/fd.hpp"
#include "coro/task.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/poll.hpp"
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
template<typename executor_type>
concept io_exceutor = executor<executor_type> and requires(executor_type e, std::coroutine_handle<> c, fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout)
{
    { e.poll(fd, op, timeout) } -> std::same_as<coro::task<poll_status>>;
};
#endif // #ifdef LIBCORO_FEATURE_NETWORKING

// clang-format on

} // namespace coro::concepts

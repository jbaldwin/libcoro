#pragma once

#include "coro/concepts/awaitable.hpp"
#include "coro/fd.hpp"
#include "coro/poll.hpp"
#include "coro/task.hpp"

#include <chrono>
#include <concepts>
#include <coroutine>

namespace coro::concepts
{

// clang-format off
template<typename type>
concept executor = requires(type t, std::coroutine_handle<> c)
{
    { t.schedule() } -> coro::concepts::awaiter;
    { t.yield() } -> coro::concepts::awaiter;
    { t.resume(c) } -> std::same_as<void>;
};

template<typename type>
concept io_exceutor = executor<type> and requires(type t, std::coroutine_handle<> c, fd_t fd, coro::poll_op op, std::chrono::milliseconds timeout)
{
    { t.poll(fd, op, timeout) } -> std::same_as<coro::task<poll_status>>;
};
// clang-format on

} // namespace coro::concepts

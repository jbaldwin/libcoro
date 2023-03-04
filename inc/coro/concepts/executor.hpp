#pragma once

#include "coro/concepts/awaitable.hpp"

#include <concepts>
#include <coroutine>

namespace coro::concepts
{
template<typename type>
concept executor = requires(type t, std::coroutine_handle<> c)
{
    {
        t.schedule()
        } -> coro::concepts::awaiter;
    {
        t.yield()
        } -> coro::concepts::awaiter;
    {
        t.resume(c)
        } -> std::same_as<void>;
    {
        t.shutdown()
        } -> std::same_as<bool>;
};

} // namespace coro::concepts

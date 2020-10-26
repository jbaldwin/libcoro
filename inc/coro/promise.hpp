#pragma once

#include "coro/awaitable.hpp"

#include <concepts>

namespace coro
{

template<typename T, typename return_type>
concept promise_type = requires(T t)
{
    { t.get_return_object() } -> std::convertible_to<std::coroutine_handle<>>;
    { t.initial_suspend() } -> awaiter_type;
    { t.final_suspend() } -> awaiter_type;
    { t.yield_value() } -> awaitable_type;
} &&
requires(T t, return_type return_value)
{
    std::same_as<decltype(t.return_void()), void> ||
    std::same_as<decltype(t.return_value(return_value)), void> ||
    requires { t.yield_value(return_value); };
};

} // namespace coro

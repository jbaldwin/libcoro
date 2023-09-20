#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace coro::concepts
{
// clang-format off
template<typename type>
concept const_buffer = requires(const type t)
{
    { t.empty() } -> std::same_as<bool>;
    { t.data() } -> std::same_as<const char*>;
    { t.size() } -> std::same_as<std::size_t>;
};

template<typename type>
concept mutable_buffer = requires(type t)
{
    { t.empty() } -> std::same_as<bool>;
    { t.data() } -> std::same_as<char*>;
    { t.size() } -> std::same_as<std::size_t>;
};
// clang-format on

} // namespace coro::concepts

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
    // The underlying data type must be a standard-layout type and trivially copyable
    typename std::remove_pointer_t<decltype(t.data())>;
    requires std::is_trivial_v<std::remove_pointer_t<decltype(t.data())>>;

    // General buffer properties
    { t.empty() } -> std::same_as<bool>;
    { t.size() } -> std::same_as<std::size_t>;

    // We check the return type of `data()` to be convertible to a const pointer to the underlying type
    { t.data() } -> std::convertible_to<const typename std::remove_pointer_t<decltype(t.data())>*>;
};

template<const_buffer buffer_type>
struct const_buffer_traits
{
    using element_type = std::add_const_t<std::remove_pointer_t<decltype(std::declval<buffer_type>().data())>>;
};

template<typename type>
concept mutable_buffer = requires(type t)
{
    // The underlying data type must be a standard-layout type and trivially copyable
    typename std::remove_pointer_t<decltype(t.data())>;
    requires std::is_trivial_v<std::remove_pointer_t<decltype(t.data())>>;

    // General buffer properties
    { t.empty() } -> std::same_as<bool>;
    { t.size() } -> std::same_as<std::size_t>;

    // We check the return type of `data()` to be a non-const pointer to the underlying type
    { t.data() } -> std::same_as<typename std::remove_pointer_t<decltype(t.data())>*>;
};

template<mutable_buffer buffer_type>
struct mutable_buffer_traits
{
    using element_type = std::remove_pointer_t<decltype(std::declval<buffer_type>().data())>;
};

// clang-format on

} // namespace coro::concepts

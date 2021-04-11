#pragma once

#include <concepts>
#include <ranges>

namespace coro::concepts
{
/**
 * Concept to require that the range contains a specific type of value.
 */
template<class T, class V>
concept range_of = std::ranges::range<T>&& std::is_same_v<V, std::ranges::range_value_t<T>>;

/**
 * Concept to require that a sized range contains a specific type of value.
 */
template<class T, class V>
concept sized_range_of = std::ranges::sized_range<T>&& std::is_same_v<V, std::ranges::range_value_t<T>>;

} // namespace coro::concepts

#pragma once

#if (__cplusplus > 202002L)
    #include <expected>
namespace coro
{
template<typename T, typename E>
using expected = std::expected<T, E>;

template<typename E>
using unexpected = std::unexpected<E>;
} // namespace coro
#else
    #include <tl/expected.hpp>
namespace coro
{
template<typename T, typename E>
using expected = tl::expected<T, E>;

template<typename E>
using unexpected = tl::unexpected<E>;
} // namespace coro
#endif

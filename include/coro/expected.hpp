#pragma once

#if (__cpp_lib_expected)
    #include <expected>
namespace coro
{
template<typename T, typename E>
using expected = std::expected<T, E>;

template<typename E>
using unexpected = std::unexpected<E>;
} // namespace coro
#else
    #include "coro/detail/tl_expected.hpp"
namespace coro
{
template<typename T, typename E>
using expected = tl::expected<T, E>;

template<typename E>
using unexpected = tl::unexpected<E>;
} // namespace coro
#endif

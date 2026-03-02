#pragma once
#include "coro/concepts/awaitable.hpp"
#include <tuple>
#include <type_traits>

namespace coro::ranges
{
namespace concepts
{

template<typename T>
struct async_stream_traits;

template<typename T>
    requires requires(T& t) {
        t.advance();
        t.get_value();
    }
struct async_stream_traits<T>
{
    using awaiter_type = decltype(std::declval<T>().advance());
    using value_type   = decltype(std::declval<T>().get_value());
};

template<class T>
using async_stream_value_t = typename async_stream_traits<T>::value_type;
template<class T>
using async_stream_awaiter_t = typename async_stream_traits<T>::awaiter_type;

template<class T>
concept async_streamable = requires(T& stream) {
    { stream.advance() } -> coro::concepts::awaitable;
    { stream.get_value() };
};

// Concept for piping
struct _async_adaptor
{
};
template<class T>
concept async_adaptor = std::derived_from<std::decay_t<T>, _async_adaptor>;
} // namespace concepts

template<typename Adaptor, typename... Args>
struct _partial : public concepts::_async_adaptor
{
    std::tuple<Args...> m_args;

    constexpr _partial(int, Args&&... args) : m_args(std::forward<Args>(args)...) {}

    template<concepts::async_streamable range_t>
    constexpr auto operator()(range_t&& range) const&
    {
        auto forwarder = [&range](const auto&... args) { return Adaptor{}(std::forward<range_t>(range), args...); };
        return std::apply(forwarder, m_args);
    }

    template<concepts::async_streamable range_t>
    constexpr auto operator()(range_t&& range) &&
    {
        auto forwarder = [&range](auto&... args)
        { return Adaptor{}(std::forward<range_t>(range), std::move(args)...); };
        return std::apply(forwarder, m_args);
    }

    template<concepts::async_streamable range_t>
    constexpr auto operator()(range_t&& range) const&& = delete;
};

template<concepts::async_adaptor Adaptor, class Range>
constexpr auto operator|(Range&& rng, Adaptor&& partial)
{
    return std::forward<Adaptor>(partial)(std::forward<Range>(rng));
}
} // namespace coro::ranges
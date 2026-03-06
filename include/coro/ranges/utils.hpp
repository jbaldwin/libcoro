#pragma once
#include "coro/concepts/awaitable.hpp"
#include "coro/task.hpp"
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>

#if defined(_MSC_VER)
    #define LIBCORO_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define LIBCORO_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
    #define LIBCORO_ALWAYS_INLINE inline
#endif

namespace coro::ranges
{
namespace concepts
{

template<typename T>
struct async_stream_traits;

template<typename T>
    requires requires(T& t) { t.next(); }
struct async_stream_traits<T>
{
    using awaiter_type = decltype(std::declval<T>().next());
    using optional_type =
        std::remove_cvref_t<typename coro::concepts::awaitable_traits<awaiter_type>::awaiter_return_type>;
    using return_type = typename optional_type::value_type;
};

template<class T>
using async_stream_return_t = typename async_stream_traits<T>::return_type;
template<class T>
using async_stream_awaiter_t = typename async_stream_traits<T>::awaiter_type;

template<class T>
concept async_streamable = requires(T& stream) {
    { stream.next() } -> coro::concepts::awaitable;
};

namespace detail
{
template<class T>
struct _wrap_return_value_for_optional
{
    using type = T;
};
template<class T>
struct _wrap_return_value_for_optional<T&>
{
    using type = std::reference_wrapper<T>;
};

} // namespace detail
template<class T>
using wrap_return_value_for_optional_t = typename detail::_wrap_return_value_for_optional<T>::type;

template<class T>
constexpr auto unwrap_return_value(T&& value) noexcept -> decltype(auto)
{
    return std::forward<T>(value);
}
template<class T>
constexpr auto unwrap_return_value(std::reference_wrapper<T> value) noexcept -> T&
{
    return value.get();
}

// Concept for piping
struct _async_adaptor
{
};
template<class T>
concept async_adaptor = std::derived_from<std::decay_t<T>, _async_adaptor>;
} // namespace concepts

// This lets a named lvalue stream  be reused
// without being moved-from. Exactly how std::ranges::ref_view works.
template<concepts::async_streamable Stream>
class ref_view
{
public:
    constexpr explicit ref_view(Stream& stream) noexcept : m_stream(stream) {}

    LIBCORO_ALWAYS_INLINE
    auto next() -> concepts::async_stream_awaiter_t<Stream> { return m_stream.next(); }

private:
    Stream& m_stream;
};

// template<concepts::async_streamable Stream>
// struct concepts::async_stream_traits<ref_view<Stream>>
//     : concepts::async_stream_traits<Stream> {};

// struct _ref
//{
//     template<concepts::async_streamable Rng>
//     constexpr auto operator()(Rng& rng) const noexcept
//     {
//         return ref_view<Rng>{rng};
//     }
// };
// inline constexpr _ref ref{};

template<class Rng, concepts::async_adaptor Adaptor>
    requires concepts::async_streamable<std::remove_cvref_t<Rng>>
constexpr auto operator|(Rng&& rng, Adaptor&& partial)
{
    // lvalue -> wrap in ref_view
    // rvalue -> move ownership
    if constexpr (std::is_lvalue_reference_v<decltype(rng)>)
    {
        return std::forward<Adaptor>(partial)(ref_view<std::remove_cvref_t<decltype(rng)>>{rng});
    }
    else
    {
        return std::forward<Adaptor>(partial)(std::forward<Rng>(rng));
    }
}

template<typename Adaptor, typename... Args>
struct _partial : public concepts::_async_adaptor
{
    std::tuple<Args...> m_args;

    template<typename... ConstructorArgs>
    constexpr explicit _partial(int, ConstructorArgs&&... args) : m_args(std::forward<ConstructorArgs>(args)...)
    {
    }

    template<class range_t>
    constexpr auto operator()(range_t&& range) const&
    {
        auto forwarder = [&range](const auto&... args) { return Adaptor{}(std::forward<range_t>(range), args...); };
        return std::apply(forwarder, m_args);
    }

    template<class range_t>
    constexpr auto operator()(range_t&& range) &&
    {
        auto forwarder = [&range](auto&... args)
        { return Adaptor{}(std::forward<range_t>(range), std::forward<decltype(args)>(args)...); };
        return std::apply(forwarder, m_args);
    }

    template<class range_t>
    constexpr auto operator()(range_t&& range) const&& = delete;
};

template<std::ranges::viewable_range range_type>
class sync_stream
{
public:
    using value_type  = std::ranges::range_value_t<range_type>;
    using return_type = value_type;

    explicit constexpr sync_stream(range_type range)
        : m_range(std::forward<range_type>(range)),
          m_it(std::begin(m_range))
    {
    }

    auto next() -> coro::task<std::optional<return_type>>
    {
        if (m_it == std::end(m_range))
        {
            co_return std::nullopt;
        }

        value_type& current = *m_it;
        ++m_it;
        co_return std::move(current);
    }

private:
    range_type                          m_range;
    std::ranges::iterator_t<range_type> m_it;
};

// Sync range overload
template<std::ranges::viewable_range Range, concepts::async_adaptor Adaptor>
constexpr auto operator|(Range&& rng, Adaptor&& partial)
{
    return std::forward<Adaptor>(partial)(sync_stream<Range>{std::forward<Range>(rng)});
}
} // namespace coro::ranges
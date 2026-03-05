#pragma once
#include "coro/concepts/awaitable.hpp"
#include "coro/task.hpp"
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

// This lets a named lvalue stream  be reused
// without being moved-from. Exactly how std::ranges::ref_view works.
template<concepts::async_streamable Stream>
class ref_view
{
public:
    constexpr explicit ref_view(Stream& stream) noexcept : m_stream(stream) {}

    LIBCORO_ALWAYS_INLINE
    auto advance() noexcept(noexcept(m_stream.advance())) -> concepts::async_stream_awaiter_t<Stream>
    {
        return m_stream.advance();
    }

    LIBCORO_ALWAYS_INLINE
    auto get_value() noexcept(noexcept(m_stream.get_value())) -> decltype(auto) { return m_stream.get_value(); }

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
        { return Adaptor{}(std::forward<range_t>(range), std::move(args)...); };
        return std::apply(forwarder, m_args);
    }

    template<class range_t>
    constexpr auto operator()(range_t&& range) const&& = delete;
};

template<std::ranges::viewable_range Range>
class sync_stream
{
public:
    explicit constexpr sync_stream(Range range) : m_range(std::forward<Range>(range)), m_it(std::begin(m_range)) {}

    auto advance() -> coro::task<bool>
    {
        if (m_it == std::end(m_range))
        {
            co_return false;
        }
        if (!m_first)
        {
            ++m_it;
        }
        m_first = false;
        co_return m_it != std::end(m_range);
    }

    auto get_value() noexcept -> decltype(auto) { return *m_it; }

private:
    Range                          m_range;
    std::ranges::iterator_t<Range> m_it;
    bool                           m_first{true};
};

// Sync range overload
template<std::ranges::viewable_range Range, concepts::async_adaptor Adaptor>
constexpr auto operator|(Range&& rng, Adaptor&& partial)
{
    return std::forward<Adaptor>(partial)(sync_stream{std::forward<Range>(rng)});
}
} // namespace coro::ranges
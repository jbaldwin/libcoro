#pragma once
#include "utils.hpp"
#include <coro/task.hpp>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t, typename value_t>
class await_view_base
{
protected:
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

    previous_stream_t m_prev_stream;

public:
    constexpr explicit await_view_base(previous_stream_t prev_stream) : m_prev_stream(prev_stream) {}

    auto next() -> awaiter_type
    {
        auto awaitable = co_await m_prev_stream.next();
        if (awaitable)
        {
            co_return co_await std::move(*awaitable);
        }
        co_return std::nullopt;
    }
};

template<concepts::async_streamable previous_stream_t>
class await_view_base<previous_stream_t, void>
{
protected:
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

    previous_stream_t m_prev_stream;

public:
    constexpr explicit await_view_base(previous_stream_t prev_stream)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream))
    {
    }

    auto next() -> coro::task<std::optional<std::monostate>>
    {
        auto awaitable = co_await m_prev_stream.next();
        if (awaitable)
        {
            co_await *awaitable;
            co_return std::monostate{};
        }
        co_return std::nullopt;
    }
};

template<
    concepts::async_streamable previous_stream_t,
    typename value_t = typename coro::concepts::awaitable_traits<
        concepts::async_stream_return_t<previous_stream_t>>::awaiter_return_type>
class await_view : public await_view_base<previous_stream_t, value_t>
{
    using base_t = await_view_base<previous_stream_t, value_t>;

public:
    using base_t::base_t;
};

struct _await
{
    template<concepts::async_streamable Rng>
    constexpr auto operator()(Rng&& rng)
    {
        return await_view<std::decay_t<Rng>>{std::forward<Rng>(rng)};
    }
};

inline constexpr auto await = _partial<_await>{0};
} // namespace coro::ranges
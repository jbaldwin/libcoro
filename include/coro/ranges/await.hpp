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

    previous_stream_t      m_prev_stream;
    std::optional<value_t> m_value;

public:
    constexpr explicit await_view_base(previous_stream_t&& prev_stream)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream))
    {
    }

    auto advance() -> coro::task<bool>
    {
        if (co_await m_prev_stream.advance())
        {
            m_value = co_await m_prev_stream.get_value();
            co_return true;
        }
        co_return false;
    }

    auto get_value() noexcept -> value_t { return std::move(*m_value); }
};

template<concepts::async_streamable previous_stream_t>
class await_view_base<previous_stream_t, void>
{
protected:
    previous_stream_t m_prev_stream;

public:
    constexpr explicit await_view_base(previous_stream_t&& prev_stream)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream))
    {
    }

    auto advance() -> coro::task<bool>
    {
        if (co_await m_prev_stream.advance())
        {
            co_await m_prev_stream.get_value();
            co_return true;
        }
        co_return false;
    }

    void get_value() noexcept {}
};

template<concepts::async_streamable previous_stream_t>
class await_view : public await_view_base<
                       previous_stream_t,
                       typename coro::concepts::awaitable_traits<
                           concepts::async_stream_value_t<previous_stream_t>>::awaiter_return_type>
{
    using base_t = await_view_base<
        previous_stream_t,
        typename coro::concepts::awaitable_traits<
            concepts::async_stream_value_t<previous_stream_t>>::awaiter_return_type>;

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
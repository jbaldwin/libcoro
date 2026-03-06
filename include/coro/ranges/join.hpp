#pragma once
#include "utils.hpp"
#include <optional>
#include <ranges>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t>
class join_view
{
private:
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;
    using container_t  = std::remove_cvref_t<concepts::async_stream_return_t<previous_stream_t>>;

    using value_type = std::ranges::range_value_t<container_t>;

    static_assert(std::ranges::sized_range<container_t>);

public:
    constexpr explicit join_view(previous_stream_t prev_stream)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream))
    {
    }

    auto next() -> coro::task<std::optional<value_type>>
    {
        if (m_value && m_cursor + 1 < m_value->size())
        {
            m_cursor += 1;
            co_return std::move((*m_value)[m_cursor]);
        }

        m_value = co_await m_prev_stream.next();
        if (m_value && m_value->size() > 0)
        {
            m_cursor = 0;
            co_return std::move((*m_value)[m_cursor]);
        }

        co_return std::nullopt;
    }

private:
    previous_stream_t m_prev_stream;

    std::optional<container_t> m_value;
    std::size_t                m_cursor{};
};

struct _join
{
    template<concepts::async_streamable Rng>
    constexpr auto operator()(Rng&& rng)
    {
        return join_view{std::forward<Rng>(rng)};
    }
};

inline constexpr auto join = _partial<_join>{0};
} // namespace coro::ranges
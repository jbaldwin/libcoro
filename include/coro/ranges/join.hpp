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
    using container_t  = concepts::async_stream_value_t<previous_stream_t>;

    using reference_t = decltype(std::declval<container_t&>()[0]);

    static_assert(std::ranges::sized_range<container_t>);

public:
    constexpr join_view(previous_stream_t prev_stream) : m_prev_stream(std::forward<previous_stream_t>(prev_stream)) {}

    auto advance() -> awaiter_type
    {
        if (m_value && m_cursor + 1 < m_value->size())
        {
            m_cursor += 1;
            co_return true;
        }

        while (co_await m_prev_stream.advance())
        {
            m_value = std::move(m_prev_stream.get_value());

            // Looping until it's not empty
            if (m_value->size() > 0)
            {
                m_cursor = 0;
                co_return true;
            }
        }

        co_return false;
    }

    auto get_value() noexcept -> reference_t { return std::move((*m_value)[m_cursor]); }

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
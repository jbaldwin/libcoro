#pragma once
#include "utils.hpp"
#include <cstdint>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t>
class take_view
{
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

public:
    constexpr take_view(previous_stream_t prev_stream, std::size_t count)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_count(count)
    {
    }

    auto next() -> awaiter_type
    {
        if (m_current < m_count)
        {
            m_current += 1;
            co_return co_await m_prev_stream.next();
        }
        co_return std::nullopt;
    }

private:
    previous_stream_t m_prev_stream;
    std::size_t       m_current{};
    const std::size_t m_count{};
};

struct _take
{
    template<concepts::async_streamable Rng>
    constexpr auto operator()(Rng&& rng, std::size_t N) const
    {
        return take_view{std::forward<Rng>(rng), N};
    }

    constexpr auto operator()(std::size_t N) const { return _partial<_take, std::size_t>{0, N}; }
};

inline constexpr _take take;
} // namespace coro::ranges
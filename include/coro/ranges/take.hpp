#pragma once
#include "utils.hpp"
#include <cstdint>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t>
class take_view
{
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

    // std::optional doesn't support reference
    using value_type = std::remove_reference_t<concepts::async_stream_value_t<previous_stream_t>>;

public:
    constexpr take_view(previous_stream_t prev_stream, std::size_t count) : m_prev_stream(prev_stream), m_count(count)
    {
    }

    auto advance() -> awaiter_type
    {
        if (m_current + 1 < m_count)
        {
            m_current += 1;
            co_return co_await m_prev_stream.advance();
        }
        co_return false;
    }

    auto get_value() noexcept(noexcept(m_prev_stream.get_value())) -> decltype(auto)
    {
        return m_prev_stream.get_value();
    }

private:
    previous_stream_t m_prev_stream;
    std::size_t       m_current{};
    const std::size_t m_count{};
};

struct _take
{
    template<concepts::async_streamable Rng>
    constexpr auto operator()(Rng&& rng, std::size_t N)
    {
        return take_view{std::forward<Rng>(rng), N};
    }

    constexpr auto operator()(std::size_t N) { return _partial<_take, std::size_t>{0, N}; }
};

inline constexpr _take take;
} // namespace coro::ranges
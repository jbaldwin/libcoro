#pragma once
#include "utils.hpp"
#include <optional>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t, class Pred>
class take_until_view
{
private:
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

public:
    constexpr take_until_view(previous_stream_t prev_stream, Pred pred)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_pred(std::forward<Pred>(pred))
    {
    }

    auto next() -> awaiter_type
    {
        auto value = co_await m_prev_stream.next();
        if (value && !m_pred(*value))
        {
            co_return std::move(value);
        }
        co_return std::nullopt;
    }

private:
    previous_stream_t m_prev_stream;
    [[no_unique_address]]
    Pred m_pred;
};

struct _take_until
{
    template<concepts::async_streamable Rng, typename Pred>
    constexpr auto operator()(Rng&& rng, Pred&& pred) const
    {
        return take_until_view{std::forward<Rng>(rng), std::forward<Pred>(pred)};
    }

    template<typename Pred>
    constexpr auto operator()(Pred&& pred) const
    {
        return _partial<_take_until, Pred>{0, std::forward<Pred>(pred)};
    }
};

inline constexpr _take_until take_until;
} // namespace coro::ranges
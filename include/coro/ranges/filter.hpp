#pragma once
#include "utils.hpp"
#include <optional>
#include <utility>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t, class filter_fn>
class filter_view
{
private:
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

public:
    constexpr filter_view(previous_stream_t prev_stream, filter_fn transform)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_filter(std::forward<filter_fn>(transform))
    {
    }

    auto next() -> awaiter_type
    {
        while (true)
        {
            auto value = co_await m_prev_stream.next();
            if (!value)
            {
                break;
            }

            if (m_filter(*value))
            {
                co_return std::forward<decltype(value)>(value);
            }
        }

        co_return std::nullopt;
    }

private:
    previous_stream_t m_prev_stream;
    [[no_unique_address]]
    filter_fn m_filter;
};

struct _filter
{
    template<concepts::async_streamable Rng, class Pred>
    constexpr auto operator()(Rng&& rng, Pred&& pred) const
    {
        return filter_view{std::forward<Rng>(rng), std::forward<Pred>(pred)};
    }

    template<class Pred>
    constexpr auto operator()(Pred&& pred) const
    {
        return _partial<_filter, Pred>{0, std::forward<Pred>(pred)};
    }
};

inline constexpr _filter filter;
} // namespace coro::ranges
#pragma once
#include "utils.hpp"
#include <optional>
#include <utility>

namespace coro::ranges
{
template<concepts::async_streamable previous_stream_t, class transform_fn>
class transform_view
{
private:
    using awaiter_type        = concepts::async_stream_awaiter_t<previous_stream_t>;
    using awaiter_return_type = concepts::async_stream_return_t<previous_stream_t>;
    using return_type         = wrap_return_value_for_optional_t<decltype(std::declval<transform_fn&>()(
        unwrap_return_value(std::declval<awaiter_return_type>())))>;

public:
    constexpr transform_view(previous_stream_t prev_stream, transform_fn transform)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_transform(std::forward<transform_fn>(transform))
    {
    }

    auto next() -> coro::task<std::optional<return_type>>
    {
        auto value = co_await m_prev_stream.next();
        if (value)
        {
            auto result = m_transform(unwrap_return_value(*value));
            co_return std::forward<decltype(result)>(result);
        }
        co_return std::nullopt;
    }

private:
    previous_stream_t m_prev_stream;
    [[no_unique_address]]
    transform_fn m_transform;
};

struct _transform
{
    template<concepts::async_streamable Rng, class Pred>
    constexpr auto operator()(Rng&& rng, Pred&& pred) const
    {
        return transform_view{std::forward<Rng>(rng), std::forward<Pred>(pred)};
    }

    template<class Pred>
    constexpr auto operator()(Pred&& pred) const
    {
        return _partial<_transform, Pred>{0, std::forward<Pred>(pred)};
    }
};

inline constexpr _transform transform;
} // namespace coro::ranges
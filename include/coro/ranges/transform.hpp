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
    using awaiter_type = concepts::async_stream_awaiter_t<previous_stream_t>;

public:
    constexpr transform_view(previous_stream_t prev_stream, transform_fn transform)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_transform(std::forward<transform_fn>(transform))
    {
    }

    auto advance() -> awaiter_type { co_return co_await m_prev_stream.advance(); }

    auto get_value() noexcept(noexcept(m_transform(m_prev_stream.get_value()))) -> decltype(auto)
    {
        return m_transform(m_prev_stream.get_value());
    }

private:
    previous_stream_t m_prev_stream;
    transform_fn      m_transform;
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
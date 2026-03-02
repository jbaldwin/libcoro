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

    // std::optional doesn't support reference
    using value_type = std::remove_reference_t<concepts::async_stream_value_t<previous_stream_t>>;

public:
    constexpr take_until_view(previous_stream_t prev_stream, Pred pred)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_pred(std::forward<Pred>(pred))
    {
    }

    auto advance() -> awaiter_type
    {
        // Got the next value from the stream
        auto has_value = co_await m_prev_stream.advance();
        if (!has_value)
        {
            co_return false;
        }

        // Save it
        m_value.emplace(std::move(m_prev_stream.get_value()));

        // Check it. true means stop
        if (m_pred(*m_value))
        {
            m_value.reset();
            co_return false;
        }

        co_return true;
    }

    auto get_value() const& noexcept -> const value_type& { return *m_value; }
    auto get_value() & noexcept -> value_type& { return *m_value; }

    auto get_value() && noexcept -> value_type { return std::move(*m_value); }

private:
    previous_stream_t         m_prev_stream;
    Pred                      m_pred;
    std::optional<value_type> m_value;
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
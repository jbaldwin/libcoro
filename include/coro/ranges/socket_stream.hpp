#pragma once
#include "coro/net/tcp/client.hpp"
#include "coro/task.hpp"
#include <vector>

namespace coro::ranges
{
template<typename T>
struct async_stream_traits;

template<typename T>
    requires requires(T& t) { t.next(); }
struct async_stream_traits<T>
{
    using value_type = typename decltype(std::declval<T&>().next())::return_type;
};

template<class T>
using async_stream_value_t = typename async_stream_traits<T>::value_type;

template<class T>
concept async_streamable = requires(T& stream) {
    { stream.next() } -> std::same_as<coro::task<async_stream_value_t<T>>>;
};

class socket_stream
{
public:
    explicit socket_stream(coro::net::tcp::client& client) : m_client(client) {};

    auto next() -> coro::task<std::optional<std::byte>>
    {
        std::byte b;
        auto      span      = std::span{&b, 1};
        auto [status, read] = co_await m_client.read_some(span);

        if (not status.is_ok())
        {
            if (status.is_closed())
            {
                co_return std::nullopt;
            }
            throw std::runtime_error(status.message());
        }
        co_return b;
    }

private:
    coro::net::tcp::client& m_client;
    //    std::vector<std::byte>  m_buffer;
};

template<async_streamable previous_stream_t, class Pred>
class take_until_view
{
private:
    using value_type = async_stream_value_t<previous_stream_t>;

    previous_stream_t m_prev_stream;
    Pred              m_pred;

public:
    constexpr take_until_view(previous_stream_t&& prev_stream, Pred&& pred)
        : m_prev_stream(std::forward<previous_stream_t>(prev_stream)),
          m_pred(std::forward<Pred>(pred))
    {
    }

    auto next() -> coro::task<value_type>
    {
        auto val = co_await m_prev_stream.next();
        if (val and not m_pred(*val))
        {
            co_return std::move(val);
        }
        co_return std::nullopt;
    }
};

template<typename Adaptor, typename... Args>
struct _partial
{
    std::tuple<Args...> m_args;

    constexpr _partial(int, Args&&... args) : m_args(std::forward<Args>(args)...) {}

    template<async_streamable range_t>
    constexpr auto operator()(range_t&& range) const&
    {
        auto forwarder = [&range](const auto&... args) { return Adaptor{}(std::forward<range_t>(range), args...); };
        return std::apply(forwarder, m_args);
    }

    template<async_streamable range_t>
    constexpr auto operator()(range_t&& range) &&
    {
        auto forwarder = [&range](auto&... args)
        { return Adaptor{}(std::forward<range_t>(range), std::move(args)...); };
        return std::apply(forwarder, m_args);
    }

    template<async_streamable range_t>
    constexpr auto operator()(range_t&& range) const&& = delete;
};

template<class Partial, class Range>
constexpr auto operator|(Range&& rng, Partial&& partial)
{
    return std::forward<Partial>(partial)(std::forward<Range>(rng));
}

struct _take_until
{
    template<async_streamable Rng, typename Pred>
    constexpr auto operator()(Rng&& rng, Pred&& pred)
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

auto to_stream(coro::net::tcp::client& client) -> socket_stream
{
    return socket_stream{client};
}

struct _to_vector
{
    template<async_streamable Rng>
    auto operator()(Rng rng) const -> coro::task<std::vector<typename async_stream_value_t<Rng>::value_type>>
    {
        std::vector<typename async_stream_value_t<Rng>::value_type> result;
        while (true)
        {
            auto val = co_await rng.next();
            if (!val)
                break;
            result.push_back(std::move(*val));
        }
        co_return result;
    }
};

inline constexpr _to_vector to_vector;
} // namespace coro::ranges
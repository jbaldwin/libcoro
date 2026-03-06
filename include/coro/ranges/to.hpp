#pragma once
#include "coro/task.hpp"
#include "utils.hpp"

namespace coro::ranges
{
template<class container_t>
struct _to : public concepts::_async_adaptor
{
public:
    template<concepts::async_streamable Rng>
    auto operator()(Rng rng) const -> coro::task<container_t>
    {
        container_t result;
        while (true)
        {
            auto value = co_await rng.advance();
            if (value)
            {
                result.emplace_back(std::move(*value));
            }
            else
            {
                break;
            }
        }
        co_return result;
    }

private:
};

template<class char_t, class traits, class alloc_t>
struct _to<std::basic_string<char_t, traits, alloc_t>> : public concepts::_async_adaptor
{
    using container_t = std::basic_string<char_t, traits, alloc_t>;

public:
    template<concepts::async_streamable Rng>
    auto operator()(Rng rng) const -> coro::task<container_t>
    {
        container_t result;
        while (true)
        {
            auto value = co_await rng.next();
            if (value)
            {
                result.push_back(std::move(*value));
            }
            else
            {
                break;
            }
        }
        co_return result;
    }

private:
};

template<class container_t>
inline constexpr _to<container_t> to;
} // namespace coro::ranges
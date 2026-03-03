#pragma once
#include "coro/task.hpp"
#include "utils.hpp"

namespace coro::ranges
{
struct _drain : public concepts::_async_adaptor
{
public:
    template<concepts::async_streamable Rng>
    auto operator()(Rng rng) const -> coro::task<void>
    {
        while (co_await rng.advance()) {}
    }
};

inline constexpr _drain drain;
} // namespace coro::ranges
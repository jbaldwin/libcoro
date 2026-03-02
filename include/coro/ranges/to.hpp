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
        while (co_await rng.advance())
        {
            result.emplace_back(std::move(rng.get_value()));
        }
        co_return result;
    }

private:
};

template<class container_t>
inline constexpr _to<container_t> to;
} // namespace coro::ranges
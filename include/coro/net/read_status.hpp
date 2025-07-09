#pragma once

namespace coro::net
{
enum class read_status
{
    ok,
    closed,
    timeout,
    error,

    udp_not_bound
};
}
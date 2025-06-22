#pragma once

namespace coro::net
{
enum class read_status
{
    ok,
    closed,
    timeout,
    error
};
}
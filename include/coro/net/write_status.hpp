#pragma once

namespace coro::net
{
enum class write_status
{
    ok,
    closed,
    timeout,
    error
};
}
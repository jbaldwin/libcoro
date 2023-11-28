#pragma once

#include <chrono>

namespace coro
{
using clock      = std::chrono::steady_clock;
using time_point = clock::time_point;
} // namespace coro

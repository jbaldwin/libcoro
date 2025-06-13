#pragma once
#include "detail/signal_unix.hpp"

namespace coro
{
using signal = detail::signal_unix;
} // namespace coro
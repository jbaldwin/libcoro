#pragma once

#include <stdexcept>

namespace coro
{
/**
 * Is thrown by various 'infinite' co_await operations if the parent object has
 * been requsted to stop, and wakes up all the awaiters in a stopped state.  The
 * awaiter's await_resume() will throw this signal to let the user know the operation
 * has been cancelled.
 */
struct stop_signal
{
};

} // namespace coro

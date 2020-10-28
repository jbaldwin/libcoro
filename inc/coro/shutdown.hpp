#pragma once

namespace coro
{
enum class shutdown_t
{
    /// Synchronously wait for all tasks to complete when calling shutdown.
    sync,
    /// Asynchronously let tasks finish on the background thread on shutdown.
    async
};

} // namespace coro

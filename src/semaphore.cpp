#include "coro/semaphore.hpp"

namespace coro
{
using namespace std::string_literals;

std::string semaphore_acquire_result_acquired = "acquired"s;
std::string semaphore_acquire_result_shutdown = "shutdown"s;
std::string semaphore_acquire_result_unknown  = "unknown"s;

auto to_string(semaphore_acquire_result result) -> const std::string&
{
    switch (result)
    {
        case semaphore_acquire_result::acquired:
            return semaphore_acquire_result_acquired;
        case semaphore_acquire_result::shutdown:
            return semaphore_acquire_result_shutdown;
    }

    return semaphore_acquire_result_unknown;
}

} // namespace coro

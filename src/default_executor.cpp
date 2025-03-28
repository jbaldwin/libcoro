#include "coro/default_executor.hpp"
#include <atomic>
#include <memory>

static coro::thread_pool::options         s_default_executor_options;
static std::atomic<coro::thread_pool*>    s_default_executor = {nullptr};
static std::shared_ptr<coro::thread_pool> s_default_executor_shared;

#ifdef LIBCORO_FEATURE_NETWORKING
static coro::io_scheduler::options         s_default_io_executor_options;
static std::atomic<coro::io_scheduler*>    s_default_io_executor = {nullptr};
static std::shared_ptr<coro::io_scheduler> s_default_io_executor_shared;
#endif

void coro::default_executor::set_executor_options(thread_pool::options thread_pool_options)
{
    s_default_executor_options = thread_pool_options;
}

std::shared_ptr<coro::thread_pool> coro::default_executor::executor()
{
    auto result = s_default_executor.load(std::memory_order::acquire);
    if (result)
    {
        return result->shared_from_this();
    }

    auto ptr = std::make_shared<coro::thread_pool>(s_default_executor_options);
    if (s_default_executor.compare_exchange_strong(
            result, ptr.get(), std::memory_order::release, std::memory_order::acquire))
    {
        s_default_executor_shared = ptr;
        return ptr;
    }

    return result->shared_from_this();
}

#ifdef LIBCORO_FEATURE_NETWORKING
void coro::default_executor::set_io_executor_options(io_scheduler::options io_scheduler_options)
{
    s_default_io_executor_options = io_scheduler_options;
}

std::shared_ptr<coro::io_scheduler> coro::default_executor::io_executor()
{
    auto result = s_default_io_executor.load(std::memory_order::acquire);
    if (result)
    {
        return result->shared_from_this();
    }

    auto ptr = coro::io_scheduler::make_shared(s_default_io_executor_options);
    if (s_default_io_executor.compare_exchange_strong(
            result, ptr.get(), std::memory_order::release, std::memory_order::acquire))
    {
        s_default_io_executor_shared = ptr;
        return ptr;
    }

    return result->shared_from_this();
}
#endif

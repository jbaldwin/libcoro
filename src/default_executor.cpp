#include "coro/default_executor.hpp"
#include <atomic>
#include <memory>

static const auto s_initialization_check_interval = std::chrono::milliseconds(1);

static coro::thread_pool::options         s_default_executor_options;
static std::atomic<bool>                  s_default_executor_init{false};
static std::atomic<coro::thread_pool*>    s_default_executor_ptr{nullptr};
static std::unique_ptr<coro::thread_pool> s_default_executor{nullptr};

#ifdef LIBCORO_FEATURE_NETWORKING
static coro::io_scheduler::options         s_default_io_executor_options;
static std::atomic<bool>                   s_default_io_executor_init{false};
static std::atomic<coro::io_scheduler*>    s_default_io_executor_ptr{nullptr};
static std::unique_ptr<coro::io_scheduler> s_default_io_executor;
#endif

void coro::default_executor::set_executor_options(thread_pool::options thread_pool_options)
{
    s_default_executor_options = std::move(thread_pool_options);
}

std::unique_ptr<coro::thread_pool>& coro::default_executor::executor()
{
    // If we're the first one here create the default executor.
    if (s_default_executor_init.exchange(true) == false)
    {
        s_default_executor = coro::thread_pool::make_unique(s_default_executor_options);
        s_default_executor_ptr.store(s_default_executor.get(), std::memory_order::release);
    }

    while (s_default_executor_ptr.load(std::memory_order::acquire) == nullptr)
    {
        std::this_thread::sleep_for(s_initialization_check_interval);
    }

    return s_default_executor;
}

#ifdef LIBCORO_FEATURE_NETWORKING
void coro::default_executor::set_io_executor_options(io_scheduler::options io_scheduler_options)
{
    s_default_io_executor_options = std::move(io_scheduler_options);
}

std::unique_ptr<coro::io_scheduler>& coro::default_executor::io_executor()
{
    // If we're the first one here create the default executor.
    if (s_default_io_executor_init.exchange(true) == false)
    {
        s_default_io_executor = coro::io_scheduler::make_unique(s_default_io_executor_options);
        s_default_io_executor_ptr.store(s_default_io_executor.get(), std::memory_order::release);
    }

    while (s_default_io_executor_ptr.load(std::memory_order::acquire) == nullptr)
    {
        std::this_thread::sleep_for(s_initialization_check_interval);
    }

    return s_default_io_executor;
}
#endif

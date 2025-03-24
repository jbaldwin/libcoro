#include "coro/default_executor.hpp"
#include <atomic>
#include <memory>

static std::atomic<coro::default_executor*> s_default_executor = {nullptr};

#ifdef LIBCORO_FEATURE_NETWORKING
coro::io_scheduler::options coro::default_executor::s_io_scheduler_options;
#else
coro::thread_pool::options coro::default_executor::s_thread_pool_options;
#endif

coro::default_executor* coro::default_executor::instance()
{
    auto* result = s_default_executor.load(std::memory_order::acquire);
    if (!result)
    {
        auto ptr = std::make_unique<default_executor>();
        if (s_default_executor.compare_exchange_strong(
                result, ptr.get(), std::memory_order::release, std::memory_order::acquire))
            result = ptr.release();
    }
    return result;
}

coro::default_executor::default_executor()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    m_io_scheduler = io_scheduler::make_shared(s_io_scheduler_options);
#else
    m_thread_pool = std::make_shared<thread_pool>(s_thread_pool_options);
#endif
}

bool coro::default_executor::spawn(coro::task<void>&& task) noexcept
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->spawn(std::move(task));
#else
    return m_thread_pool->spawn(std::move(task));
#endif
}

bool coro::default_executor::resume(std::coroutine_handle<> handle)
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->resume(handle);
#else
    return m_thread_pool->resume(handle);
#endif
}

std::size_t coro::default_executor::size()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->size();
#else
    return m_thread_pool->size();
#endif
}

bool coro::default_executor::empty()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->empty();
#else
    return m_thread_pool->empty();
#endif
}

void coro::default_executor::shutdown()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    m_io_scheduler->shutdown();
#else
    m_thread_pool->shutdown();
#endif
}

#ifdef LIBCORO_FEATURE_NETWORKING
auto coro::default_executor::schedule() -> coro::io_scheduler::schedule_operation
{
    return m_io_scheduler->schedule();
}
#else
auto coro::default_executor::schedule() -> coro::thread_pool::operation
{
    return m_thread_pool->schedule();
}
#endif

#ifdef LIBCORO_FEATURE_NETWORKING

void coro::default_executor::set_io_scheduler_options(io_scheduler::options io_scheduler_options)
{
    s_io_scheduler_options = io_scheduler_options;
}

std::shared_ptr<coro::io_scheduler> coro::default_executor::get_io_scheduler()
{
    return m_io_scheduler;
}

#else

void coro::default_executor::set_thread_pool_options(thread_pool::options thread_pool_options)
{
    s_thread_pool_options = thread_pool_options;
}

std::shared_ptr<coro::thread_pool> coro::default_executor::get_thread_pool()
{
    return m_thread_pool;
}

#endif

#include "coro/facade.hpp"
#include <atomic>
#include <memory>

static std::atomic<coro::facade*> s_facade = {nullptr};

#ifdef LIBCORO_FEATURE_NETWORKING
coro::io_scheduler::options coro::facade::s_io_scheduler_options;
#else
coro::thread_pool::options coro::facade::s_thread_pool_options;
#endif

coro::facade* coro::facade::instance()
{
    auto* result = s_facade.load(std::memory_order::acquire);
    if (!result)
    {
        auto ptr = std::make_unique<facade>();
        if (s_facade.compare_exchange_strong(result, ptr.get(), std::memory_order::release, std::memory_order::acquire))
            result = ptr.release();
    }
    return result;
}

coro::facade::facade()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    m_io_scheduler = io_scheduler::make_shared(s_io_scheduler_options);
#else
    m_thread_pool = std::make_shared<thread_pool>(s_thread_pool_options);
#endif
}

bool coro::facade::spawn(coro::task<void>&& task) noexcept
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->spawn(std::move(task));
#else
    return m_thread_pool->spawn(std::move(task));
#endif
}

bool coro::facade::resume(std::coroutine_handle<> handle)
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->resume(handle);
#else
    return m_thread_pool->resume(handle);
#endif
}

std::size_t coro::facade::size()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->size();
#else
    return m_thread_pool->size();
#endif
}

bool coro::facade::empty()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    return m_io_scheduler->empty();
#else
    return m_thread_pool->empty();
#endif
}

void coro::facade::shutdown()
{
#ifdef LIBCORO_FEATURE_NETWORKING
    m_io_scheduler->shutdown();
#else
    m_thread_pool->shutdown();
#endif
}

auto coro::facade::yield() -> coro::task<void>
{
#ifdef LIBCORO_FEATURE_NETWORKING
    co_await m_io_scheduler->yield();
#else
    co_await m_thread_pool->yield();
#endif
}

auto coro::facade::schedule() -> coro::task<void>
{
#ifdef LIBCORO_FEATURE_NETWORKING
    co_await m_io_scheduler->schedule();
#else
    co_await m_thread_pool->schedule();
#endif
}

#ifdef LIBCORO_FEATURE_NETWORKING

void coro::facade::set_io_scheduler_options(io_scheduler::options io_scheduler_options)
{
    s_io_scheduler_options = io_scheduler_options;
}

std::shared_ptr<coro::io_scheduler> coro::facade::get_io_scheduler()
{
    return m_io_scheduler;
}

#else

void coro::facade::set_thread_pool_options(thread_pool::options thread_pool_options)
{
    s_thread_pool_options = thread_pool_options;
}

std::shared_ptr<coro::thread_pool> coro::facade::get_thread_pool()
{
    return m_thread_pool;
}

#endif

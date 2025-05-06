#include "coro/condition_variable.hpp"
#include <cassert>

static constexpr const auto s_stop_source_check_interval = std::chrono::milliseconds(200);

namespace coro
{

#ifdef LIBCORO_FEATURE_NETWORKING

auto detail::strategy_based_on_io_scheduler::wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration)
    -> task<std::cv_status>
{
    auto mtx = lock.mutex();

    auto             wo = std::make_shared<wait_operation>(*this, std::move(lock));
    std::stop_source stop_source;
    auto             result = co_await when_any(wait_task(wo, stop_source), timeout_task(wo, duration, stop_source));

    // cancel a late task
    stop_source.request_stop();

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return std::holds_alternative<timeout_status>(result) ? std::cv_status::timeout : std::cv_status::no_timeout;
}

auto detail::strategy_based_on_io_scheduler::wait_task(
    std::shared_ptr<detail::strategy_based_on_io_scheduler::wait_operation> wo,
    std::stop_source                                                        stop_source) -> task<bool>
{
    auto stop = [wo]()
    {
        if (!wo->m_awaiting_coroutine.done())
            wo->m_awaiting_coroutine.resume();
    };
    std::stop_callback<decltype(stop)> stop_callback(stop_source.get_token(), stop);

    #if !defined(__clang__) && defined(__GNUC__) && __GNUC__ < 11
    struct wait_operation_proxy
    {
        explicit wait_operation_proxy(std::shared_ptr<detail::strategy_based_on_io_scheduler::wait_operation> wo)
            : m_wo(wo)
        {
        }

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            return m_wo->await_suspend(awaiting_coroutine);
        }
        auto await_resume() noexcept -> void {};

    private:
        std::shared_ptr<detail::strategy_based_on_io_scheduler::wait_operation> m_wo;
    };

    co_await wait_operation_proxy(wo);
    #else
    co_await *wo;
    #endif

    co_return true;
}

auto detail::strategy_based_on_io_scheduler::timeout_task(
    std::shared_ptr<detail::strategy_based_on_io_scheduler::wait_operation> wo,
    std::chrono::milliseconds                                               timeout,
    std::stop_source                                                        stop_source) -> coro::task<timeout_status>
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    assert(!m_scheduler.expired());
    auto deadline   = steady_clock::now() + timeout;
    auto stop_token = stop_source.get_token();

    while ((steady_clock::now() < deadline) && !stop_source.stop_requested())
    {
        auto remain       = duration_cast<milliseconds>(deadline - steady_clock::now());
        auto next_timeout = std::min(remain, s_stop_source_check_interval);
        if (auto sched = m_scheduler.lock())
        {
            co_await sched->schedule_after(next_timeout);
        }
    }

    if (!stop_token.stop_requested())
    {
        extract_waiter(wo.get());
    }
    co_return timeout_status::timeout;
}

void detail::strategy_based_on_io_scheduler::insert_waiter(wait_operation* waiter) noexcept
{
    auto ptr = m_free_links.acquire();
    ptr->waiter.store(waiter, std::memory_order::relaxed);
    waiter->m_link.store(ptr.get(), std::memory_order::relaxed);
    m_internal_waiters.push(ptr.release());
}

void detail::strategy_based_on_io_scheduler::extract_waiter(wait_operation* waiter) noexcept
{
    auto link = waiter->m_link.exchange(nullptr, std::memory_order::acq_rel);
    if (!link)
        return;

    link->waiter.store(nullptr, std::memory_order::release);
}

detail::strategy_based_on_io_scheduler::strategy_based_on_io_scheduler(std::shared_ptr<io_scheduler> io_scheduler)
    : m_scheduler(io_scheduler),
      m_free_links(
          std::function<std::unique_ptr<wait_operation_link>()>([]()
                                                                { return std::make_unique<wait_operation_link>(); }),
          [](wait_operation_link* ptr) { ptr->waiter.store(nullptr, std::memory_order::relaxed); })
{
    assert(io_scheduler);
}

detail::strategy_based_on_io_scheduler::~strategy_based_on_io_scheduler()
{
    while (auto opt = m_internal_waiters.pop())
    {
        m_free_links.release(opt.value());
    }
}

task<void> detail::strategy_based_on_io_scheduler::wait(scoped_lock& lock)
{
    auto mtx = lock.mutex();

    co_await wait_for_notify(std::move(lock));

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return;
}

void detail::strategy_based_on_io_scheduler::notify_one() noexcept
{
    assert(!m_scheduler.expired());

    while (auto waiter_link = extract_one())
    {
        if (auto sched = m_scheduler.lock())
        {
            if (auto* waiter = waiter_link->waiter.exchange(nullptr, std::memory_order::acq_rel))
            {
                if (waiter->m_link.exchange(nullptr, std::memory_order::acq_rel))
                {
                    sched->resume(waiter->m_awaiting_coroutine);
                    break;
                }
            }
        }
    }
}

void detail::strategy_based_on_io_scheduler::notify_all() noexcept
{
    assert(!m_scheduler.expired());

    if (auto sched = m_scheduler.lock())
    {
        while (auto waiter_link = extract_one())
        {
            if (auto* waiter = waiter_link->waiter.exchange(nullptr, std::memory_order::acq_rel))
            {
                if (waiter->m_link.exchange(nullptr, std::memory_order::acq_rel))
                {
                    sched->resume(waiter->m_awaiting_coroutine);
                }
            }
        }
    }
}

detail::strategy_based_on_io_scheduler::wait_operation_link_unique_ptr
    detail::strategy_based_on_io_scheduler::extract_one()
{
    return {m_internal_waiters.pop().value_or(nullptr), m_free_links.pool_deleter()};
}

detail::strategy_based_on_io_scheduler::wait_operation::wait_operation(
    detail::strategy_based_on_io_scheduler& strategy, scoped_lock&& lock)
    : m_strategy(strategy),
      m_lock(std::move(lock))
{
}

detail::strategy_based_on_io_scheduler::wait_operation::~wait_operation()
{
    m_strategy.extract_waiter(this);
}

auto detail::strategy_based_on_io_scheduler::wait_operation::await_suspend(
    std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_strategy.insert_waiter(this);
    if (auto sched = m_strategy.m_scheduler.lock())
    {
        m_lock.unlock(*sched);
    }
    return true;
}

#endif

detail::strategy_base::wait_operation::wait_operation(detail::strategy_base& strategy, scoped_lock&& lock)
    : m_strategy(strategy),
      m_lock(std::move(lock))
{
}

bool detail::strategy_base::wait_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
{
    m_awaiting_coroutine = awaiting_coroutine;
    m_strategy.m_internal_waiters.push(this);
    m_lock.unlock();
    return true;
}

auto detail::strategy_base::wait(scoped_lock& lock) -> task<void>
{
    auto mtx = lock.mutex();

    co_await wait_for_notify(std::move(lock));

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return;
}

void detail::strategy_base::notify_one() noexcept
{
    if (auto waiter = m_internal_waiters.pop().value_or(nullptr))
    {
        waiter->m_awaiting_coroutine.resume();
    }
}

void detail::strategy_base::notify_all() noexcept
{
    while (auto waiter = m_internal_waiters.pop().value_or(nullptr))
    {
        waiter->m_awaiting_coroutine.resume();
    }
}

} // namespace coro

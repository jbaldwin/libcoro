#include "coro/shared_mutex.hpp"
#include "coro/thread_pool.hpp"

namespace coro
{
shared_scoped_lock::~shared_scoped_lock()
{
    unlock();
}

auto shared_scoped_lock::unlock() -> void
{
    if (m_shared_mutex != nullptr)
    {
        if (m_exclusive)
        {
            m_shared_mutex->unlock();
        }
        else
        {
            m_shared_mutex->unlock_shared();
        }

        m_shared_mutex = nullptr;
    }
}

shared_mutex::shared_mutex(coro::thread_pool& tp) : m_thread_pool(tp)
{
}

auto shared_mutex::lock_operation::await_ready() const noexcept -> bool
{
    if (m_exclusive)
    {
        return m_shared_mutex.try_lock();
    }
    else
    {
        return m_shared_mutex.try_lock_shared();
    }
}

auto shared_mutex::lock_operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    std::unique_lock lk{m_shared_mutex.m_mutex};
    // Its possible the lock has been released between await_ready() and await_suspend(), double
    // check and make sure we are not going to suspend when nobody holds the lock.
    if (m_exclusive)
    {
        if (m_shared_mutex.try_lock_locked(lk))
        {
            return false;
        }
    }
    else
    {
        if (m_shared_mutex.try_lock_shared_locked(lk))
        {
            return false;
        }
    }

    // For sure the lock is currently held in a manner that it cannot be acquired, suspend ourself
    // at the end of the waiter list.

    if (m_shared_mutex.m_tail_waiter == nullptr)
    {
        m_shared_mutex.m_head_waiter = this;
        m_shared_mutex.m_tail_waiter = this;
    }
    else
    {
        m_shared_mutex.m_tail_waiter->m_next = this;
        m_shared_mutex.m_tail_waiter         = this;
    }

    // If this is an exclusive lock acquire then mark it as so so that shared locks after this
    // exclusive one will also suspend so this exclusive lock doens't get starved.
    if (m_exclusive)
    {
        ++m_shared_mutex.m_exclusive_waiters;
    }

    m_awaiting_coroutine = awaiting_coroutine;
    return true;
}

auto shared_mutex::try_lock_shared() -> bool
{
    // To acquire the shared lock the state must be one of two states:
    //   1) unlocked
    //   2) shared locked with zero exclusive waiters
    //          Zero exclusive waiters prevents exclusive starvation if shared locks are
    //          always continuously happening.

    std::unique_lock lk{m_mutex};
    return try_lock_shared_locked(lk);
}

auto shared_mutex::try_lock() -> bool
{
    // To acquire the exclusive lock the state must be unlocked.
    std::unique_lock lk{m_mutex};
    return try_lock_locked(lk);
}

auto shared_mutex::unlock_shared() -> void
{
    std::unique_lock lk{m_mutex};
    --m_shared_users;

    // Only wake waiters from shared state if all shared users have completed.
    if (m_shared_users == 0)
    {
        if (m_head_waiter != nullptr)
        {
            wake_waiters(lk);
        }
        else
        {
            m_state = state::unlocked;
        }
    }
}

auto shared_mutex::unlock() -> void
{
    std::unique_lock lk{m_mutex};
    if (m_head_waiter != nullptr)
    {
        wake_waiters(lk);
    }
    else
    {
        m_state = state::unlocked;
    }
}
auto shared_mutex::try_lock_shared_locked(std::unique_lock<std::mutex>& lk) -> bool
{
    if (m_state == state::unlocked)
    {
        // If the shared mutex is unlocked put it into shared mode and add ourself as using the lock.
        m_state = state::locked_shared;
        ++m_shared_users;
        lk.unlock();
        return true;
    }
    else if (m_state == state::locked_shared && m_exclusive_waiters == 0)
    {
        // If the shared mutex is in a shared locked state and there are no exclusive waiters
        // the add ourself as using the lock.
        ++m_shared_users;
        lk.unlock();
        return true;
    }

    // If the lock is in shared mode but there are exclusive waiters then we will also wait so
    // the writers are not starved.

    // If the lock is in exclusive mode already then we need to wait.

    return false;
}

auto shared_mutex::try_lock_locked(std::unique_lock<std::mutex>& lk) -> bool
{
    if (m_state == state::unlocked)
    {
        m_state = state::locked_exclusive;
        lk.unlock();
        return true;
    }
    return false;
}

auto shared_mutex::wake_waiters(std::unique_lock<std::mutex>& lk) -> void
{
    // First determine what the next lock state will be based on the first waiter.
    if (m_head_waiter->m_exclusive)
    {
        // If its exclusive then only this waiter can be woken up.
        m_state                   = state::locked_exclusive;
        lock_operation* to_resume = m_head_waiter;
        m_head_waiter             = m_head_waiter->m_next;
        --m_exclusive_waiters;
        if (m_head_waiter == nullptr)
        {
            m_tail_waiter = nullptr;
        }

        // Since this is an exclusive lock waiting we can resume it directly.
        lk.unlock();
        to_resume->m_awaiting_coroutine.resume();
    }
    else
    {
        // If its shared then we will scan forward and awake all shared waiters onto the given
        // thread pool so they can run in parallel.
        m_state = state::locked_shared;
        do
        {
            lock_operation* to_resume = m_head_waiter;
            m_head_waiter             = m_head_waiter->m_next;
            if (m_head_waiter == nullptr)
            {
                m_tail_waiter = nullptr;
            }
            ++m_shared_users;

            m_thread_pool.resume(to_resume->m_awaiting_coroutine);
        } while (m_head_waiter != nullptr && !m_head_waiter->m_exclusive);

        // Cannot unlock until the entire set of shared waiters has been traversed.  I think this
        // makes more sense than allocating space for all the shared waiters, unlocking, and then
        // resuming in a batch?
        lk.unlock();
    }
}

} // namespace coro

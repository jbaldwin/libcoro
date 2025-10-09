#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/mutex.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <coroutine>

namespace coro
{
template<concepts::executor executor_type>
class shared_mutex;

namespace detail
{
template<concepts::executor executor_type>
struct shared_lock_operation
{
    explicit shared_lock_operation(coro::shared_mutex<executor_type>& shared_mutex, const bool exclusive)
        : m_shared_mutex(shared_mutex),
          m_exclusive(exclusive)
    {}
    ~shared_lock_operation() = default;

    shared_lock_operation(const shared_lock_operation&) = delete;
    shared_lock_operation(shared_lock_operation&&) = delete;
    auto operator=(const shared_lock_operation&) -> shared_lock_operation& = delete;
    auto operator=(shared_lock_operation&&) -> shared_lock_operation& = delete;

    auto await_ready() const noexcept -> bool
    {
        // If either mode can be acquired, unlock the internal mutex and resume.

        if (m_exclusive)
        {
            if (m_shared_mutex.try_lock_locked())
            {
                m_shared_mutex.m_mutex.unlock();
                return true;
            }
        }
        else if (m_shared_mutex.try_lock_shared_locked())
        {
            m_shared_mutex.m_mutex.unlock();
            return true;
        }

        return false;
    }

    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
    {
        // For sure the lock is currently held in a manner that it cannot be acquired, suspend ourself
        // at the end of the waiter list.

        auto* tail_waiter = m_shared_mutex.m_tail_waiter.load(std::memory_order::acquire);

        if (tail_waiter == nullptr)
        {
            m_shared_mutex.m_head_waiter = this;
            m_shared_mutex.m_tail_waiter = this;
        }
        else
        {
            tail_waiter->m_next = this;
            m_shared_mutex.m_tail_waiter         = this;
        }

        // If this is an exclusive lock acquire then mark it as so so that shared locks after this
        // exclusive one will also suspend so this exclusive lock doesn't get starved.
        if (m_exclusive)
        {
            ++m_shared_mutex.m_exclusive_waiters;
        }

        m_awaiting_coroutine = awaiting_coroutine;
        m_shared_mutex.m_mutex.unlock();
        return true;
    }

    auto await_resume() noexcept -> void { }

protected:
    friend class coro::shared_mutex<executor_type>;

    std::coroutine_handle<> m_awaiting_coroutine;
    shared_lock_operation* m_next{nullptr};
    coro::shared_mutex<executor_type>& m_shared_mutex;
    bool m_exclusive{false};
};

} // namespace detail

template<concepts::executor executor_type>
class shared_mutex
{
public:
    /**
     * @param e The executor for when multiple shared waiters can be woken up at the same time,
     *          each shared waiter will be scheduled to immediately run on this executor in
     *          parallel.
     */
    explicit shared_mutex(std::unique_ptr<executor_type>& e) : m_executor(e.get())
    {
        if (m_executor == nullptr)
        {
            throw std::runtime_error{"coro::shared_mutex cannot have a nullptr executor"};
        }
    }
    ~shared_mutex() = default;

    shared_mutex(const shared_mutex&)                    = delete;
    shared_mutex(shared_mutex&&)                         = delete;
    auto operator=(const shared_mutex&) -> shared_mutex& = delete;
    auto operator=(shared_mutex&&) -> shared_mutex&      = delete;

    /**
     * Acquires the lock in a shared state, executes the scoped task, and then unlocks the shared lock.
     * Because unlocking a coro::shared_mutex is a task this scoped version cannot be returned as a RAII
     * object due to destructors not being able to be co_await'ed.
     * @param scoped_task The user's scoped task to execute after acquiring the shared lock.
     */
    [[nodiscard]] auto scoped_lock_shared(coro::task<void> scoped_task) -> coro::task<void>
    {
        co_await m_mutex.lock();
        co_await detail::shared_lock_operation<executor_type>{*this, false};
        co_await scoped_task;
        co_await unlock_shared();
        co_return;
    }

    /**
     * Acquires the lock in an exclusive state, executes the scoped task, and then unlocks the exclusive lock.
     * Because unlocking a coro::shared_mutex is a task this scoped version cannot be returned as a RAII
     * object due to destructors not being able to be co_await'ed.
     * @param scoped_task The user's scoped task to execute after acquiring the exclusive lock.
     */
    [[nodiscard]] auto scoped_lock(coro::task<void> scoped_task) -> coro::task<void>
    {
        co_await m_mutex.lock();
        co_await detail::shared_lock_operation<executor_type>{*this, true};
        co_await scoped_task;
        co_await unlock();
        co_return;
    }

    /**
     * Acquires the lock in a shared state. The shared_mutex must be unlock_shared() to release.
     * @return task
     */
    [[nodiscard]] auto lock_shared() -> coro::task<void>
    {
        co_await m_mutex.lock();
        co_await detail::shared_lock_operation<executor_type>{*this, false};
        co_return;
    }

    /**
     * Acquires the lock in an exclusive state. The shared_mutex must be unlock()'ed to release.
     * @return task
     */
    [[nodiscard]] auto lock() -> coro::task<void>
    {
        co_await m_mutex.lock();
        co_await detail::shared_lock_operation<executor_type>{*this, true};
        co_return;
    }

    /**
     * @return True if the lock could immediately be acquired in a shared state.
     */
    [[nodiscard]] auto try_lock_shared() -> bool
    {
        // To acquire the shared lock the state must be one of two states:
        //   1) unlocked
        //   2) shared locked with zero exclusive waiters
        //          Zero exclusive waiters prevents exclusive starvation if shared locks are
        //          always continuously happening.

        if (m_mutex.try_lock())
        {
            coro::scoped_lock lk{m_mutex};
            return try_lock_shared_locked();
        }
        return false;
    }

    /**
     * @return True if the lock could immediately be acquired in an exclusive state.
     */
    [[nodiscard]] auto try_lock() -> bool
    {
        // To acquire the exclusive lock the state must be unlocked.
        if (m_mutex.try_lock())
        {
            coro::scoped_lock lk{m_mutex};
            return try_lock_locked();
        }
        return false;
    }

    /**
     * Unlocks a single shared state user. *REQUIRES* that the lock was first acquired exactly once
     * via `lock_shared()` or `try_lock_shared() -> True` before being called, otherwise undefined
     * behavior.
     *
     * If the shared user count drops to zero and this lock has an exclusive waiter then the exclusive
     * waiter acquires the lock.
     */
    [[nodiscard]] auto unlock_shared() -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        auto users = m_shared_users.fetch_sub(1, std::memory_order::acq_rel);

        // If this is the final unlock_shared() see if there is anyone to wakeup.
        if (users == 1)
        {
            auto* head_waiter = m_head_waiter.load(std::memory_order::acquire);
            if (head_waiter != nullptr)
            {
                wake_waiters(lk, head_waiter);
            }
            else
            {
                m_state = state::unlocked;
            }
        }

        co_return;
    }

    /**
     * Unlocks the mutex from its exclusive state. If there is a following exclusive waiter then
     * that exclusive waiter acquires the lock.  If there are 1 or more shared waiters then all the
     * shared waiters acquire the lock in a shared state in parallel and are resumed on the original
     * executor this shared mutex was created with.
     */
    [[nodiscard]] auto unlock() -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        auto* head_waiter = m_head_waiter.load(std::memory_order::acquire);
        if (head_waiter != nullptr)
        {
            wake_waiters(lk, head_waiter);
        }
        else
        {
            m_state = state::unlocked;
        }

        co_return;
    }

    /**
     * @brief Gets the executor that drives the shared mutex.
     *
     * @return executor_type&
     */
    [[nodiscard]] auto executor() -> executor_type&
    {
        return *m_executor;
    }

private:
    friend struct detail::shared_lock_operation<executor_type>;

    enum class state
    {
        /// @brief The shared mutex is unlocked.
        unlocked,
        /// @brief The shared mutex is locked in shared mode.
        locked_shared,
        /// @brief The shared mutex is locked in exclusive mode.
        locked_exclusive
    };

    /// @brief This executor is for resuming multiple shared waiters.
    executor_type* m_executor{nullptr};
    /// @brief Exclusive access for mutating the shared mutex's state.
    coro::mutex m_mutex;
    /// @brief The current state of the shared mutex.
    std::atomic<state> m_state{state::unlocked};

    /// @brief The current number of shared users that have acquired the lock.
    std::atomic<uint64_t> m_shared_users{0};
    /// @brief The current number of exclusive waiters waiting to acquire the lock.  This is used to block
    ///        new incoming shared lock attempts so the exclusive waiter is not starved.
    std::atomic<uint64_t> m_exclusive_waiters{0};

    std::atomic<detail::shared_lock_operation<executor_type>*> m_head_waiter{nullptr};
    std::atomic<detail::shared_lock_operation<executor_type>*> m_tail_waiter{nullptr};

    auto try_lock_shared_locked() -> bool
    {
        if (m_state == state::unlocked)
        {
            // If the shared mutex is unlocked put it into shared mode and add ourself as using the lock.
            m_state = state::locked_shared;
            ++m_shared_users;
            return true;
        }
        else if (m_state == state::locked_shared && m_exclusive_waiters == 0)
        {
            // If the shared mutex is in a shared locked state and there are no exclusive waiters
            // the add ourself as using the lock.
            ++m_shared_users;
            return true;
        }

        // If the lock is in shared mode but there are exclusive waiters then we will also wait so
        // the writers are not starved.

        // If the lock is in exclusive mode already then we need to wait.

        return false;
    }

    auto try_lock_locked() -> bool
    {
        if (m_state == state::unlocked)
        {
            m_state = state::locked_exclusive;
            return true;
        }
        return false;
    }

    auto wake_waiters(coro::scoped_lock& lk, detail::shared_lock_operation<executor_type>* head_waiter) -> void
    {
        // First determine what the next lock state will be based on the first waiter.
        if (head_waiter->m_exclusive)
        {
            // If its exclusive then only this waiter can be woken up.
            m_state.store(state::locked_exclusive, std::memory_order::release);
            if (head_waiter->m_next == nullptr)
            {
                // This is the final waiter, set the list to null.
                m_head_waiter.store(nullptr, std::memory_order::release);
                m_tail_waiter.store(nullptr, std::memory_order::release);
            }
            else
            {
                // Advance the head waiter to next.
                m_head_waiter.store(head_waiter->m_next, std::memory_order::release);
            }

            m_exclusive_waiters.fetch_sub(1, std::memory_order::release);

            // Since this is an exclusive lock waiting we can resume it directly.
            lk.unlock();
            head_waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            // If its shared then we will scan forward and awake all shared waiters onto the given
            // thread pool so they can run in parallel.
            m_state.store(state::locked_shared, std::memory_order::release);
            while (true)
            {
                auto* to_resume = m_head_waiter.load(std::memory_order::acquire);
                if (to_resume == nullptr || to_resume->m_exclusive)
                {
                    break;
                }

                if (to_resume->m_next == nullptr)
                {
                    m_head_waiter.store(nullptr, std::memory_order::release);
                    m_tail_waiter.store(nullptr, std::memory_order::release);
                }
                else
                {
                    m_head_waiter.store(to_resume->m_next, std::memory_order::release);
                }

                m_shared_users.fetch_add(1, std::memory_order::release);

                m_executor->resume(to_resume->m_awaiting_coroutine);
            }

            // Cannot unlock until the entire set of shared waiters has been traversed. I think this
            // makes more sense than allocating space for all the shared waiters, unlocking, and then
            // resuming in a batch?
            lk.unlock();
        }
    }
};

} // namespace coro

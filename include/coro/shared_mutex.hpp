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

/**
 * A scoped RAII lock holder for a coro::shared_mutex.  It will call the appropriate unlock() or
 * unlock_shared() based on how the coro::shared_mutex was originally acquired, either shared or
 * exclusive modes.
 */
template<concepts::executor executor_type>
class shared_scoped_lock
{
public:
    shared_scoped_lock(shared_mutex<executor_type>& sm, bool exclusive) : m_shared_mutex(&sm), m_exclusive(exclusive) {}

    /**
     * Unlocks the mutex upon this shared scoped lock destructing.
     */
    ~shared_scoped_lock() { unlock(); }

    shared_scoped_lock(const shared_scoped_lock&) = delete;
    shared_scoped_lock(shared_scoped_lock&& other)
        : m_shared_mutex(std::exchange(other.m_shared_mutex, nullptr)),
          m_exclusive(other.m_exclusive)
    {
    }

    auto operator=(const shared_scoped_lock&) -> shared_scoped_lock& = delete;
    auto operator=(shared_scoped_lock&& other) noexcept -> shared_scoped_lock&
    {
        if (std::addressof(other) != this)
        {
            m_shared_mutex = std::exchange(other.m_shared_mutex, nullptr);
            m_exclusive    = other.m_exclusive;
        }
        return *this;
    }

    /**
     * Unlocks the shared mutex prior to this lock going out of scope.
     */
    auto unlock() -> void
    {
        if (m_shared_mutex != nullptr)
        {
            if (!m_shared_mutex->m_executor->spawn(make_unlock_task(m_shared_mutex, m_exclusive)))
            {
                // If for some reason it fails to spawn block so this mutex isn't unusable.
                coro::sync_wait(make_unlock_task(m_shared_mutex, m_exclusive));
            }
        }
    }

private:
    shared_mutex<executor_type>* m_shared_mutex{nullptr};
    bool                         m_exclusive{false};

    static auto make_unlock_task(shared_mutex<executor_type>* shared_mutex, bool exclusive) -> coro::task<void>
    {
        std::cerr << "make_unlock_task() start\n";
        // This function is spawned and detached from the lifetime of the unlocking scoped lock.

        if (shared_mutex != nullptr)
        {
            if (exclusive)
            {
                std::cerr << "unlock() exclusive\n";
                co_await shared_mutex->unlock();
            }
            else
            {
                std::cerr << "unlock() shared\n";
                co_await shared_mutex->unlock_shared();
            }

            shared_mutex = nullptr;
        }
        else
        {
            std::cerr << "unlock() nullptr\n";
        }
        std::cerr << "make_unlock_task() end\n";
    }
};

template<concepts::executor executor_type>
class shared_mutex
{
public:
    /**
     * @param e The executor for when multiple shared waiters can be woken up at the same time,
     *          each shared waiter will be scheduled to immediately run on this executor in
     *          parallel.
     */
    explicit shared_mutex(std::shared_ptr<executor_type> e) : m_executor(std::move(e))
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

    struct lock_operation
    {
        lock_operation(shared_mutex& sm, bool exclusive) : m_shared_mutex(sm), m_exclusive(exclusive) {}

        auto await_ready() const noexcept -> bool
        {
            // If either mode can be acquired, unlock the internal mutex and resume.

            if (m_exclusive && m_shared_mutex.try_lock_locked())
            {
                m_shared_mutex.m_mutex.unlock();
                return true;
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
            // exclusive one will also suspend so this exclusive lock doens't get starved.
            if (m_exclusive)
            {
                ++m_shared_mutex.m_exclusive_waiters;
            }

            m_awaiting_coroutine = awaiting_coroutine;
            m_shared_mutex.m_mutex.unlock();
            return true;
        }

        auto await_resume() noexcept -> shared_scoped_lock<executor_type>
        {
            return shared_scoped_lock{m_shared_mutex, m_exclusive};
        }

    private:
        friend class shared_mutex;

        shared_mutex&           m_shared_mutex;
        bool                    m_exclusive{false};
        std::coroutine_handle<> m_awaiting_coroutine;
        lock_operation*         m_next{nullptr};
    };

    /**
     * Locks the mutex in a shared state.  If there are any exclusive waiters then the shared waiters
     * will also wait so the exclusive waiters are not starved.
     */
    [[nodiscard]] auto lock_shared() -> coro::task<shared_scoped_lock<executor_type>>
    {
        co_await m_mutex.lock();
        co_return co_await lock_operation{*this, false};
    }

    /**
     * Locks the mutex in an exclusive state.
     */
    [[nodiscard]] auto lock() -> coro::task<shared_scoped_lock<executor_type>>
    {
        co_await m_mutex.lock();
        co_return co_await lock_operation{*this, true};
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
     * Unlocks a single shared state user.  *REQUIRES* that the lock was first acquired exactly once
     * via `lock_shared()` or `try_lock_shared() -> True` before being called, otherwise undefined
     * behavior.
     *
     * If the shared user count drops to zero and this lock has an exclusive waiter then the exclusive
     * waiter acquires the lock.
     */
    [[nodiscard]] auto unlock_shared() -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
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

        co_return;
    }

    /**
     * Unlocks the mutex from its exclusive state.  If there is a following exclusive watier then
     * that exclusive waiter acquires the lock.  If there are 1 or more shared waiters then all the
     * shared waiters acquire the lock in a shared state in parallel and are resumed on the original
     * executor this shared mutex was created with.
     */
    [[nodiscard]] auto unlock() -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        if (m_head_waiter != nullptr)
        {
            wake_waiters(lk);
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
     * @return std::shared_ptr<executor_type>
     */
    [[nodiscard]] auto executor() -> std::shared_ptr<executor_type>
    {
        return m_executor;
    }

private:
    friend struct lock_operation;
    friend struct shared_scoped_lock<executor_type>;

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
    std::shared_ptr<executor_type> m_executor{nullptr};
    /// @brief Exclusive access for mutating the shared mutex's state.
    coro::mutex m_mutex;
    /// @brief The current state of the shared mutex.
    std::atomic<state> m_state{state::unlocked};

    /// @brief The current number of shared users that have acquired the lock.
    std::atomic<uint64_t> m_shared_users{0};
    /// @brief The current number of exclusive waiters waiting to acquire the lock.  This is used to block
    ///        new incoming shared lock attempts so the exclusive waiter is not starved.
    std::atomic<uint64_t> m_exclusive_waiters{0};

    std::atomic<lock_operation*> m_head_waiter{nullptr};
    std::atomic<lock_operation*> m_tail_waiter{nullptr};

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

    auto wake_waiters(coro::scoped_lock& lk) -> void
    {
        // First determine what the next lock state will be based on the first waiter.
        if (m_head_waiter.load()->m_exclusive)
        {
            // If its exclusive then only this waiter can be woken up.
            m_state                   = state::locked_exclusive;
            lock_operation* to_resume = m_head_waiter.load();
            m_head_waiter             = m_head_waiter.load()->m_next;
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
                lock_operation* to_resume = m_head_waiter.load();
                m_head_waiter             = m_head_waiter.load()->m_next;
                if (m_head_waiter == nullptr)
                {
                    m_tail_waiter = nullptr;
                }
                ++m_shared_users;

                m_executor->resume(to_resume->m_awaiting_coroutine);
            } while (m_head_waiter != nullptr && !m_head_waiter.load()->m_exclusive);

            // Cannot unlock until the entire set of shared waiters has been traversed.  I think this
            // makes more sense than allocating space for all the shared waiters, unlocking, and then
            // resuming in a batch?
            lk.unlock();
        }
    }
};

} // namespace coro

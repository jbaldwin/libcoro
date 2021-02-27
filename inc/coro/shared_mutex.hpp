#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>

namespace coro
{
class shared_mutex;
class thread_pool;

/**
 * A scoped RAII lock holder for a coro::shared_mutex.  It will call the appropriate unlock() or
 * unlock_shared() based on how the coro::shared_mutex was originally acquired, either shared or
 * exclusive modes.
 */
class shared_scoped_lock
{
public:
    shared_scoped_lock(shared_mutex& sm, bool exclusive) : m_shared_mutex(&sm), m_exclusive(exclusive) {}

    /**
     * Unlocks the mutex upon this shared scoped lock destructing.
     */
    ~shared_scoped_lock();

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
    auto unlock() -> void;

private:
    shared_mutex* m_shared_mutex{nullptr};
    bool          m_exclusive{false};
};

class shared_mutex
{
public:
    /**
     * @param tp The thread pool for when multiple shared waiters can be woken up at the same time,
     *           each shared waiter will be scheduled to immediately run on this thread pool in
     *           parralell.
     */
    explicit shared_mutex(coro::thread_pool& tp);
    ~shared_mutex() = default;

    shared_mutex(const shared_mutex&) = delete;
    shared_mutex(shared_mutex&&)      = delete;
    auto operator=(const shared_mutex&) -> shared_mutex& = delete;
    auto operator=(shared_mutex&&) -> shared_mutex& = delete;

    struct lock_operation
    {
        lock_operation(shared_mutex& sm, bool exclusive) : m_shared_mutex(sm), m_exclusive(exclusive) {}

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> shared_scoped_lock { return shared_scoped_lock{m_shared_mutex, m_exclusive}; }

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
    [[nodiscard]] auto lock_shared() -> lock_operation { return lock_operation{*this, false}; }

    /**
     * Locks the mutex in an exclusive state.
     */
    [[nodiscard]] auto lock() -> lock_operation { return lock_operation{*this, true}; }

    /**
     * @return True if the lock could immediately be acquired in a shared state.
     */
    auto try_lock_shared() -> bool;

    /**
     * @return True if the lock could immediately be acquired in an exclusive state.
     */
    auto try_lock() -> bool;

    /**
     * Unlocks a single shared state user.  *REQUIRES* that the lock was first acquired exactly once
     * via `lock_shared()` or `try_lock_shared() -> True` before being called, otherwise undefined
     * behavior.
     *
     * If the shared user count drops to zero and this lock has an exclusive waiter then the exclusive
     * waiter acquires the lock.
     */
    auto unlock_shared() -> void;

    /**
     * Unlocks the mutex from its exclusive state.  If there is a following exclusive watier then
     * that exclusive waiter acquires the lock.  If there are 1 or more shared waiters then all the
     * shared waiters acquire the lock in a shared state in parallel and are resumed on the original
     * thread pool this shared mutex was created with.
     */
    auto unlock() -> void;

private:
    friend class lock_operation;

    enum class state
    {
        unlocked,
        locked_shared,
        locked_exclusive
    };

    /// This thread pool is for resuming multiple shared waiters.
    coro::thread_pool& m_thread_pool;

    std::mutex m_mutex;

    state m_state{state::unlocked};

    /// The current number of shared users that have acquired the lock.
    uint64_t m_shared_users{0};
    /// The current number of exclusive waiters waiting to acquire the lock.  This is used to block
    /// new incoming shared lock attempts so the exclusive waiter is not starved.
    uint64_t m_exclusive_waiters{0};

    lock_operation* m_head_waiter{nullptr};
    lock_operation* m_tail_waiter{nullptr};

    auto try_lock_shared_locked(std::unique_lock<std::mutex>& lk) -> bool;
    auto try_lock_locked(std::unique_lock<std::mutex>& lk) -> bool;

    auto wake_waiters(std::unique_lock<std::mutex>& lk) -> void;
};

} // namespace coro

#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>
#include <utility>

namespace coro
{
class mutex;

/**
 * A scoped RAII lock holder, just like std::lock_guard or std::scoped_lock in that the coro::mutex
 * is always unlocked unpon this coro::scoped_lock going out of scope.  It is possible to unlock the
 * coro::mutex prior to the end of its current scope by manually calling the unlock() function.
 */
class scoped_lock
{
    friend class mutex;

public:
    enum class lock_strategy
    {
        /// The lock is already acquired, adopt it as the new owner.
        adopt
    };

    explicit scoped_lock(mutex& m, lock_strategy strategy = lock_strategy::adopt) : m_mutex(&m)
    {
        // Future -> support acquiring the lock?  Not sure how to do that without being able to
        // co_await in the constructor.
        (void)strategy;
    }

    /**
     * Unlocks the mutex upon this shared lock destructing.
     */
    ~scoped_lock();

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&& other) : m_mutex(std::exchange(other.m_mutex, nullptr)) {}
    auto operator=(const scoped_lock&) -> scoped_lock& = delete;
    auto operator=(scoped_lock&& other) noexcept -> scoped_lock&
    {
        if (std::addressof(other) != this)
        {
            m_mutex = std::exchange(other.m_mutex, nullptr);
        }
        return *this;
    }

    /**
     * Unlocks the scoped lock prior to it going out of scope.  Calling this multiple times has no
     * additional affect after the first call.
     */
    auto unlock() -> void;

private:
    mutex* m_mutex{nullptr};
};

class mutex
{
public:
    explicit mutex() noexcept : m_state(const_cast<void*>(unlocked_value())) {}
    ~mutex() = default;

    mutex(const mutex&)                    = delete;
    mutex(mutex&&)                         = delete;
    auto operator=(const mutex&) -> mutex& = delete;
    auto operator=(mutex&&) -> mutex&      = delete;

    struct lock_operation
    {
        explicit lock_operation(mutex& m) : m_mutex(m) {}

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> scoped_lock { return scoped_lock{m_mutex}; }

    private:
        friend class mutex;

        mutex&                  m_mutex;
        std::coroutine_handle<> m_awaiting_coroutine;
        lock_operation*         m_next{nullptr};
    };

    /**
     * To acquire the mutex's lock co_await this function.  Upon acquiring the lock it returns
     * a coro::scoped_lock which will hold the mutex until the coro::scoped_lock destructs.
     * @return A co_await'able operation to acquire the mutex.
     */
    [[nodiscard]] auto lock() -> lock_operation { return lock_operation{*this}; };

    /**
     * Attempts to lock the mutex.
     * @return True if the mutex lock was acquired, otherwise false.
     */
    auto try_lock() -> bool;

    /**
     * Releases the mutex's lock.
     */
    auto unlock() -> void;

private:
    friend class lock_operation;

    /// unlocked -> state == unlocked_value()
    /// locked but empty waiter list == nullptr
    /// locked with waiters == lock_operation*
    std::atomic<void*> m_state;

    /// A list of grabbed internal waiters that are only accessed by the unlock()'er.
    lock_operation* m_internal_waiters{nullptr};

    /// Inactive value, this cannot be nullptr since we want nullptr to signify that the mutex
    /// is locked but there are zero waiters, this makes it easy to CAS new waiters into the
    /// m_state linked list.
    auto unlocked_value() const noexcept -> const void* { return &m_state; }
};

} // namespace coro

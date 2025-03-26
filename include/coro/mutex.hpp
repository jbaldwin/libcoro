#pragma once

#include "coro/concepts/executor.hpp"
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

    explicit scoped_lock(class coro::mutex& m, lock_strategy strategy = lock_strategy::adopt) : m_mutex(&m)
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
    scoped_lock(scoped_lock&& other) : m_mutex(other.m_mutex.exchange(nullptr, std::memory_order::acq_rel)) {}
    auto operator=(const scoped_lock&) -> scoped_lock& = delete;
    auto operator=(scoped_lock&& other) noexcept -> scoped_lock&
    {
        if (std::addressof(other) != this)
        {
            m_mutex.store(other.m_mutex.exchange(nullptr, std::memory_order::acq_rel), std::memory_order::release);
        }
        return *this;
    }

    /**
     * Unlocks the scoped lock prior to it going out of scope.  Calling this multiple times has no
     * additional affect after the first call.
     */
    auto unlock() -> void;

    /**
     * Unlocks the scoped lock prior to it going out of scope.  Calling this multiple times has no
     * additional affect after the first call. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    auto unlock(executor_type& e) -> void;

    class coro::mutex* mutex() const noexcept;

private:
    std::atomic<class coro::mutex*> m_mutex{nullptr};
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

    /**
     * Releases the mutex's lock. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    auto unlock(executor_type& e) -> void
    {
        if (m_internal_waiters == nullptr)
        {
            void* current = m_state.load(std::memory_order::relaxed);
            if (current == nullptr)
            {
                // If there are no internal waiters and there are no atomic waiters, attempt to set the
                // mutex as unlocked.
                if (m_state.compare_exchange_strong(
                        current,
                        const_cast<void*>(unlocked_value()),
                        std::memory_order::release,
                        std::memory_order::relaxed))
                {
                    return; // The mutex is now unlocked with zero waiters.
                }
                // else we failed to unlock, someone added themself as a waiter.
            }

            // There are waiters on the atomic list, acquire them and update the state for all others.
            m_internal_waiters = static_cast<lock_operation*>(m_state.exchange(nullptr, std::memory_order::acq_rel));

            // Should internal waiters be reversed to allow for true FIFO, or should they be resumed
            // in this reverse order to maximum throuhgput?  If this list ever gets 'long' the reversal
            // will take some time, but it might guarantee better latency across waiters.  This LIFO
            // middle ground on the atomic waiters means the best throughput at the cost of the first
            // waiter possibly having added latency based on the queue length of waiters.  Either way
            // incurs a cost but this way for short lists will most likely be faster even though it
            // isn't completely fair.
        }

        // assert m_internal_waiters != nullptr

        lock_operation* to_resume = m_internal_waiters;
        m_internal_waiters        = m_internal_waiters->m_next;
        e.resume(to_resume->m_awaiting_coroutine);
    }

private:
    friend struct lock_operation;

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

template<concepts::executor executor_type>
inline auto scoped_lock::unlock(executor_type& e) -> void
{
    if (auto mtx = m_mutex.load(std::memory_order::acquire))
    {
        std::atomic_thread_fence(std::memory_order::release);

        // Only allow a scoped lock to unlock the mutex a single time.
        m_mutex = nullptr;
        mtx->unlock(e);
    }
}

} // namespace coro

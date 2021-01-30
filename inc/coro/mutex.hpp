#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>

namespace coro
{
class mutex;

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
    ~scoped_lock();

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&& other) : m_mutex(std::exchange(other.m_mutex, nullptr)) {}
    auto operator=(const scoped_lock&) -> scoped_lock& = delete;
    auto operator                                      =(scoped_lock&& other) -> scoped_lock&
    {
        if (std::addressof(other) != this)
        {
            m_mutex = std::exchange(other.m_mutex, nullptr);
        }
        return *this;
    }

    /**
     * Unlocks the scoped lock prior to it going out of scope.
     */
    auto unlock() -> void;

private:
    mutex* m_mutex{nullptr};
};

class mutex
{
public:
    explicit mutex() noexcept = default;
    ~mutex()                  = default;

    mutex(const mutex&) = delete;
    mutex(mutex&&)      = delete;
    auto operator=(const mutex&) -> mutex& = delete;
    auto operator=(mutex&&) -> mutex& = delete;

    struct lock_operation
    {
        explicit lock_operation(mutex& m) : m_mutex(m) {}

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> scoped_lock { return scoped_lock{m_mutex}; }

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
    // friend class scoped_lock;
    friend class lock_operation;

    std::atomic<bool> m_state{false};
    std::mutex        m_waiter_mutex{};
    lock_operation*   m_head_waiter{nullptr};
    lock_operation*   m_tail_waiter{nullptr};
};

} // namespace coro

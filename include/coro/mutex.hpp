#pragma once

#include "coro/task.hpp"

#include <atomic>
#include <coroutine>
#include <mutex>
#include <utility>

namespace coro
{
class mutex;
class scoped_lock;
class condition_variable;

namespace detail
{

struct lock_operation_base
{
    explicit lock_operation_base(coro::mutex& m) : m_mutex(m) {}
    virtual ~lock_operation_base() = default;

    lock_operation_base(const lock_operation_base&) = delete;
    lock_operation_base(lock_operation_base&&) = delete;
    auto operator=(const lock_operation_base&) -> lock_operation_base& = delete;
    auto operator=(lock_operation_base&&) -> lock_operation_base& = delete;

    auto await_ready() const noexcept -> bool;
    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;

protected:
    friend class coro::mutex;

    coro::mutex&            m_mutex;
    std::coroutine_handle<> m_awaiting_coroutine;
    lock_operation_base*    m_next{nullptr};
};

template<typename return_type>
struct lock_operation : public lock_operation_base
{
    explicit lock_operation(coro::mutex& m) : lock_operation_base(m) {}
    ~lock_operation() override = default;

    lock_operation(const lock_operation&) = delete;
    lock_operation(lock_operation&&) = delete;
    auto operator=(const lock_operation&) -> lock_operation& = delete;
    auto operator=(lock_operation&&) -> lock_operation& = delete;

    auto await_resume() noexcept -> return_type
    {
        if constexpr (std::is_same_v<scoped_lock, return_type>)
        {
            return scoped_lock{m_mutex};
        }
        else
        {
            return;
        }
    }
};

} // namespace detail

/**
 * A scoped RAII lock holder similar to std::unique_lock.
 */
class scoped_lock
{
    friend class coro::mutex;
    friend class coro::condition_variable; // cv.wait() functions need to be able do unlock and re-lock

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
    scoped_lock(scoped_lock&& other)
        : m_mutex(std::exchange(other.m_mutex, nullptr)) {}
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
     * Unlocks the scoped lock prior to it going out of scope.
     */
    auto unlock() -> void;

private:
    class coro::mutex* m_mutex{nullptr};
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

    /**
     * @brief To acquire the mutex's lock co_await this function. Upon acquiring the lock it returns a coro::scoped_lock
     *        which will hold the mutex until the coro::scoped_lock destructs.
     * @return A co_await'able operation to acquire the mutex.
     */
    [[nodiscard]] auto scoped_lock() -> detail::lock_operation<scoped_lock> { return detail::lock_operation<coro::scoped_lock>{*this}; }

    /**
     * @brief Locks the mutex.
     *
     * @return detail::lock_operation<void>
     */
    [[nodiscard]] auto lock() -> detail::lock_operation<void> { return detail::lock_operation<void>{*this}; }

    /**
     * Attempts to lock the mutex.
     * @return True if the mutex lock was acquired, otherwise false.
     */
    [[nodiscard]] auto try_lock() -> bool;

    /**
     * Releases the mutex's lock.
     */
    auto unlock() -> void;

private:
    friend struct detail::lock_operation_base;

    /// unlocked -> state == unlocked_value()
    /// locked but empty waiter list == nullptr
    /// locked with waiters == lock_operation*
    std::atomic<void*> m_state;

    /// A list of grabbed internal waiters that are only accessed by the unlock()'er.
    detail::lock_operation_base* m_internal_waiters{nullptr};

    /// Inactive value, this cannot be nullptr since we want nullptr to signify that the mutex
    /// is locked but there are zero waiters, this makes it easy to CAS new waiters into the
    /// m_state linked list.
    auto unlocked_value() const noexcept -> const void* { return &m_state; }
};

} // namespace coro

#pragma once

#include "coro/detail/awaiter_list.hpp"
#include "coro/expected.hpp"
#include "coro/export.hpp"
#include "coro/mutex.hpp"
#include "coro/sync_wait.hpp"

#include <atomic>
#include <coroutine>
#include <string>

namespace coro
{

enum class semaphore_acquire_result
{
    /// @brief The semaphore was acquired.
    acquired,
    /// @brief The semaphore is shutting down, it has not been acquired.
    shutdown
};

extern CORO_EXPORT std::string semaphore_acquire_result_acquired;
extern CORO_EXPORT std::string semaphore_acquire_result_shutdown;
extern CORO_EXPORT std::string semaphore_acquire_result_unknown;

auto to_string(semaphore_acquire_result result) -> const std::string&;

template<std::ptrdiff_t max_value>
class semaphore;

namespace detail
{

template<std::ptrdiff_t max_value>
class acquire_operation
{
public:
    explicit acquire_operation(semaphore<max_value>& s) : m_semaphore(s) { }

    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        // If the semaphore is shutdown or a resources can be acquired without suspending release the lock and resume execution.
        if (m_semaphore.m_shutdown.load(std::memory_order::acquire) || m_semaphore.try_acquire())
        {
            m_semaphore.m_mutex.unlock();
            return true;
        }

        return false;
    }

    auto await_suspend(const std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
    {
        // Check again now that we've set up the coroutine frame, the state could have changed.
        if (await_ready())
        {
            return false;
        }

        m_awaiting_coroutine = awaiting_coroutine;
        detail::awaiter_list_push(m_semaphore.m_acquire_waiters, this);
        m_semaphore.m_mutex.unlock();
        return true;
    }

    [[nodiscard]] auto await_resume() const -> semaphore_acquire_result
    {
        if (m_semaphore.m_shutdown.load(std::memory_order::acquire))
        {
            return semaphore_acquire_result::shutdown;
        }
        return semaphore_acquire_result::acquired;
    }

    acquire_operation<max_value>* m_next{nullptr};
    semaphore<max_value>&         m_semaphore;
    std::coroutine_handle<>       m_awaiting_coroutine;
};

} // namespace detail

template<std::ptrdiff_t max_value>
class semaphore
{
public:
    explicit semaphore(const std::ptrdiff_t starting_value)
        : m_counter(starting_value)
    { }

    ~semaphore() { coro::sync_wait(shutdown()); }

    semaphore(const semaphore&) = delete;
    semaphore(semaphore&&)      = delete;

    auto operator=(const semaphore&) noexcept -> semaphore& = delete;
    auto operator=(semaphore&&) noexcept -> semaphore&      = delete;

    /**
     * @brief Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will suspend and wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> coro::task<semaphore_acquire_result>
    {
        co_await m_mutex.lock();
        co_return co_await detail::acquire_operation<max_value>{*this};
    }

    /**
     * @brief Releases a resources back to the semaphore, if the semaphore is already at value() == max() this does nothing.
     * @return
     */
    [[nodiscard]] auto release() -> coro::task<void>
    {
        co_await m_mutex.lock();
        // Do not resume or increment resources past the max_value.
        if (value() == max())
        {
            m_mutex.unlock();
            co_return;
        }

        // If there are any waiters just transfer resource ownership to the waiter.
        auto* waiter = detail::awaiter_list_pop(m_acquire_waiters);
        if (waiter != nullptr)
        {
            m_mutex.unlock();
            waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            // Release the resource.
            m_counter.fetch_add(1, std::memory_order::release);
            m_mutex.unlock();
        }
    }

    /**
     * @brief Attempts to acquire a resource if there are any resources available.
     * @return True if the acquire operation was able to acquire a resource.
     */
    auto try_acquire() -> bool
    {
        auto expected = m_counter.load(std::memory_order::acquire);
        do
        {
            if (expected <= 0)
            {
                return false;
            }
        } while (!m_counter.compare_exchange_weak(expected, expected - 1, std::memory_order::acq_rel, std::memory_order::acquire));

        return true;
    }

    /**
     * @return The maximum number of resources the semaphore can contain.
     */
    [[nodiscard]] static constexpr auto max() noexcept -> std::ptrdiff_t { return max_value; }

    /**
     * @return The current number of resources available to acquire for this semaphore.
     */
    [[nodiscard]] auto value() const noexcept -> std::ptrdiff_t { return m_counter.load(std::memory_order::acquire); }

    /**
     * Stops the semaphore and will notify all release/acquire waiters to wake up in a failed state.
     * Once this is set it cannot be un-done and all future oprations on the semaphore will fail.
     */
    [[nodiscard]] auto shutdown() noexcept -> coro::task<void>
    {
        if (is_shutdown()) {
            co_return;
        }
        auto lock = co_await m_mutex.scoped_lock();
        bool expected{false};
        if (m_shutdown.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
        {
            auto* waiter = detail::awaiter_list_pop_all(m_acquire_waiters);
            lock.unlock();
            while (waiter != nullptr)
            {
                auto* next = waiter->m_next;
                waiter->m_awaiting_coroutine.resume();
                waiter = next;
            }
        }
    }

    /**
     * @return True if this semaphore has been shutdown.
     */
    [[nodiscard]] auto is_shutdown() const -> bool { return m_shutdown.load(std::memory_order::acquire); }

private:
    friend class detail::acquire_operation<max_value>;

    /// @brief The current number of resources that are available to acquire.
    std::atomic<std::ptrdiff_t> m_counter;
    /// @brief The current list of awaiters attempting to acquire the semaphore.
    std::atomic<detail::acquire_operation<max_value>*> m_acquire_waiters{nullptr};
    /// @brief mutex used to do acquire and release operations
    coro::mutex m_mutex;
    /// @brief Flag to denote that all waiters should be woken up with the shutdown result.
    std::atomic<bool> m_shutdown{false};
};

} // namespace coro

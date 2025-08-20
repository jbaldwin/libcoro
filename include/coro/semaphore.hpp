#pragma once

#include "coro/detail/awaiter_list.hpp"
#include "coro/expected.hpp"
#include "coro/export.hpp"

#include <atomic>
#include <coroutine>
#include <mutex>
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

    auto await_ready() const noexcept -> bool
    {
        if (m_semaphore.m_shutdown.load(std::memory_order::acquire))
        {
            return true;
        }
        return m_semaphore.try_acquire();
    }

    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
    {
        // Check again now that we've setup the coroutine frame, the state could have changed.
        if (await_ready())
        {
            return false;
        }

        m_awaiting_coroutine = awaiting_coroutine;
        detail::awaiter_list_push(m_semaphore.m_acquire_waiters, this);
        return true;
    }

    auto await_resume() const -> semaphore_acquire_result
    {
        if (m_semaphore.m_shutdown.load(std::memory_order::acquire))
        {
            return semaphore_acquire_result::shutdown;
        }
        return semaphore_acquire_result::acquired;
    }

    acquire_operation<max_value>*      m_next{nullptr};
    semaphore<max_value>&              m_semaphore;
    std::coroutine_handle<> m_awaiting_coroutine;
};

} // namespace detail

template<std::ptrdiff_t max_value>
class semaphore
{
public:
    explicit semaphore(std::ptrdiff_t starting_value)
        : m_counter(starting_value)
    { }

    ~semaphore() { shutdown(); }

    semaphore(const semaphore&) = delete;
    semaphore(semaphore&&)      = delete;

    auto operator=(const semaphore&) noexcept -> semaphore& = delete;
    auto operator=(semaphore&&) noexcept -> semaphore&      = delete;

    auto release() -> void
    {
        // If there are any waiters just transfer ownership to the waiter.
        auto* waiter = detail::awaiter_list_pop(m_acquire_waiters);
        if (waiter != nullptr)
        {
            waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            // Attempt to increment the counter only up to max_value.
            auto current = m_counter.load(std::memory_order::acquire);
            do
            {
                if (current >= max_value)
                {
                    return;
                }
            } while (!m_counter.compare_exchange_weak(current, current + 1, std::memory_order::acq_rel, std::memory_order::acquire));
        }
    }

    /**
     * Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> detail::acquire_operation<max_value> { return detail::acquire_operation<max_value>{*this}; }

    /**
     * Attemtps to acquire a resource if there is any resources available.
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
    constexpr auto max() const noexcept -> std::ptrdiff_t { return max_value; }

    /**
     * The current number of resources available in this semaphore.
     */
    auto value() const noexcept -> std::ptrdiff_t { return m_counter.load(std::memory_order::acquire); }

    /**
     * Stops the semaphore and will notify all release/acquire waiters to wake up in a failed state.
     * Once this is set it cannot be un-done and all future oprations on the semaphore will fail.
     */
    auto shutdown() noexcept -> void
    {
        bool expected{false};
        if (m_shutdown.compare_exchange_strong(expected, true, std::memory_order::release, std::memory_order::relaxed))
        {
            auto* waiter = detail::awaiter_list_pop_all(m_acquire_waiters);
            while (waiter != nullptr)
            {
                auto* next = waiter->m_next;
                waiter->m_awaiting_coroutine.resume();
                waiter = next;
            }
        }
    }

private:
    friend class detail::acquire_operation<max_value>;

    std::atomic<std::ptrdiff_t> m_counter;
    /// @brief The current list of awaiters attempting to acquire the semaphore.
    std::atomic<detail::acquire_operation<max_value>*> m_acquire_waiters{nullptr};
    /// @brief Flag to denote that all waiters should be woken up with the shutdown result.
    std::atomic<bool> m_shutdown{false};
};

} // namespace coro

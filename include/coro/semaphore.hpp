#pragma once

#include <coro/expected.hpp>
#include <coro/export.hpp>

#include <atomic>
#include <coroutine>
#include <mutex>
#include <string>

namespace coro
{

enum class semaphore_acquire_result
{
    acquired,
    shutdown
};

extern CORO_EXPORT std::string semaphore_acquire_result_acquired;
extern CORO_EXPORT std::string semaphore_acquire_result_shutdown;
extern CORO_EXPORT std::string semaphore_acquire_result_unknown;

auto to_string(semaphore_acquire_result result) -> const std::string&;

template<std::ptrdiff_t max_value>
class semaphore;

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

    class acquire_operation
    {
    public:
        explicit acquire_operation(semaphore& s) : m_semaphore(s) { }

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
            std::unique_lock lk{m_semaphore.m_waiter_mutex};
            if (m_semaphore.m_shutdown.load(std::memory_order::relaxed))
            {
                return false;
            }

            if (m_semaphore.try_acquire())
            {
                return false;
            }

            if (m_semaphore.m_acquire_waiters == nullptr)
            {
                m_semaphore.m_acquire_waiters = this;
            }
            else
            {
                // Set our next to the current head.
                m_next = m_semaphore.m_acquire_waiters;
                // Set the semaphore head to this.
                m_semaphore.m_acquire_waiters = this;
            }

            m_awaiting_coroutine = awaiting_coroutine;
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

    private:
        template<std::ptrdiff_t>
        friend class semaphore;

        semaphore&              m_semaphore;
        std::coroutine_handle<> m_awaiting_coroutine;
        acquire_operation*      m_next{nullptr};
    };

    auto release() -> void
    {
        // It seems like the atomic counter could be incremented, but then resuming a waiter could have
        // a race between a new acquirer grabbing the just incremented resource value from us.  So its
        // best to check if there are any waiters first, and transfer owernship of the resource thats
        // being released directly to the waiter to avoid this problem.

        std::unique_lock lk{m_waiter_mutex};
        if (m_acquire_waiters != nullptr)
        {
            acquire_operation* to_resume = m_acquire_waiters;
            m_acquire_waiters            = m_acquire_waiters->m_next;
            lk.unlock();

            // This will transfer ownership of the resource to the resumed waiter.
            to_resume->m_awaiting_coroutine.resume();
        }
        else
        {
            // Optimistically increment, if we're over the max remove it.
            if (m_counter.fetch_add(1, std::memory_order::relaxed) >= max_value)
            {
                m_counter.fetch_sub(1, std::memory_order::relaxed);
            }
        }
    }

    /**
     * Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> acquire_operation { return acquire_operation{*this}; }

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
            while (true)
            {
                std::unique_lock lk{m_waiter_mutex};
                if (m_acquire_waiters != nullptr)
                {
                    acquire_operation* to_resume = m_acquire_waiters;
                    m_acquire_waiters            = m_acquire_waiters->m_next;
                    lk.unlock();

                    to_resume->m_awaiting_coroutine.resume();
                }
                else
                {
                    break;
                }
            }
        }
    }

private:
    friend class acquire_operation;

    std::atomic<std::ptrdiff_t> m_counter;

    std::mutex         m_waiter_mutex{};
    acquire_operation* m_acquire_waiters{nullptr};

    std::atomic<bool> m_shutdown{false};
};

} // namespace coro

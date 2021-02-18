#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>

namespace coro
{
class semaphore
{
public:
    explicit semaphore(std::ptrdiff_t least_max_value_and_starting_value);
    explicit semaphore(std::ptrdiff_t least_max_value, std::ptrdiff_t starting_value);
    ~semaphore();

    semaphore(const semaphore&) = delete;
    semaphore(semaphore&&)      = delete;

    auto operator=(const semaphore&) noexcept -> semaphore& = delete;
    auto operator=(semaphore&&) noexcept -> semaphore& = delete;

    // struct release_operation
    // {
    //     explicit release_operation(semaphore& s);

    //     auto await_ready() const noexcept -> bool;
    //     auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
    //     auto await_resume() const noexcept -> bool;

    //     semaphore&              m_semaphore;
    //     std::coroutine_handle<> m_awaiting_coroutine;
    //     release_operation*      m_next{nullptr};
    // };

    struct acquire_operation
    {
        explicit acquire_operation(semaphore& s);

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() const noexcept -> bool;

        semaphore&              m_semaphore;
        std::coroutine_handle<> m_awaiting_coroutine;
        acquire_operation*      m_next{nullptr};
    };

    /**
     * Releases a resource back to the semaphore, if the semaphore is at its max value then this
     * will wait until a resource as been acquired.
     */
    // [[nodiscard]] auto release() -> release_operation { return release_operation{*this}; }

    auto release() -> void;

    /**
     * Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> acquire_operation { return acquire_operation{*this}; }

    // /**
    //  * Attempts to release a resource if there is space available.
    //  * @return True if the release operation was able to release a resource.
    //  */
    // auto try_release() -> bool;

    /**
     * Attemtps to acquire a resource if there is any resources available.
     * @return True if the acquire operation was able to acquire a resource.
     */
    auto try_acquire() -> bool;

    /**
     * @return The maximum number of resources the semaphore can contain.
     */
    auto max() const noexcept -> std::ptrdiff_t { return m_least_max_value; }

    /**
     * The current number of resources available in this semaphore.
     */
    auto value() const noexcept -> std::ptrdiff_t { return m_counter.load(std::memory_order::relaxed); }

    /**
     * Stops the semaphore and will notify all release/acquire waiters to wake up in a failed state.
     * Once this is set it cannot be un-done and all future oprations on the semaphore will fail.
     */
    auto stop_notify_all() noexcept -> void;

private:
    friend class release_operation;
    friend class acquire_operation;

    // auto try_release_locked(std::unique_lock<std::mutex>& lk) -> bool;
    // auto try_acquire_locked(std::unique_lock<std::mutex>& lk) -> bool;

    const std::ptrdiff_t        m_least_max_value;
    std::atomic<std::ptrdiff_t> m_counter;

    std::mutex m_waiter_mutex{};
    // release_operation* m_release_waiters{nullptr};
    acquire_operation* m_acquire_waiters{nullptr};

    bool m_notify_all_set{false};
};

} // namespace coro

#pragma once

#include <expected/expected.hpp>

#include <atomic>
#include <coroutine>
#include <mutex>
#include <string>

namespace coro
{
class semaphore
{
public:
    enum class acquire_result
    {
        acquired,
        semaphore_stopped
    };

    static std::string acquire_result_acquired;
    static std::string acquire_result_semaphore_stopped;
    static std::string acquire_result_unknown;

    static auto to_string(acquire_result ar) -> const std::string&
    {
        switch (ar)
        {
            case acquire_result::acquired:
                return acquire_result_acquired;
            case acquire_result::semaphore_stopped:
                return acquire_result_semaphore_stopped;
        }

        return acquire_result_unknown;
    }

    explicit semaphore(std::ptrdiff_t least_max_value_and_starting_value);
    explicit semaphore(std::ptrdiff_t least_max_value, std::ptrdiff_t starting_value);
    ~semaphore();

    semaphore(const semaphore&) = delete;
    semaphore(semaphore&&)      = delete;

    auto operator=(const semaphore&) noexcept -> semaphore& = delete;
    auto operator=(semaphore&&) noexcept -> semaphore&      = delete;

    class acquire_operation
    {
    public:
        explicit acquire_operation(semaphore& s);

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() const -> acquire_result;

    private:
        friend semaphore;

        semaphore&              m_semaphore;
        std::coroutine_handle<> m_awaiting_coroutine;
        acquire_operation*      m_next{nullptr};
    };

    auto release() -> void;

    /**
     * Acquires a resource from the semaphore, if the semaphore has no resources available then
     * this will wait until a resource becomes available.
     */
    [[nodiscard]] auto acquire() -> acquire_operation { return acquire_operation{*this}; }

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
    auto notify_waiters() noexcept -> void;

private:
    friend class release_operation;
    friend class acquire_operation;

    const std::ptrdiff_t        m_least_max_value;
    std::atomic<std::ptrdiff_t> m_counter;

    std::mutex         m_waiter_mutex{};
    acquire_operation* m_acquire_waiters{nullptr};

    std::atomic<bool> m_notify_all_set{false};
};

} // namespace coro

#pragma once

#include "coro/io_scheduler.hpp"
#include "coro/mutex.hpp"

namespace coro
{

/**
 * The @ref coro::condition_variable is a thread safe async tool used with a @ref coro::mutex (@ref coro::scoped_lock)
 * to suspend one or more @ref coro::task until another @ref coro::task both modifies a shared variable
 *  (the condition) and notifies the @ref coro::condition_variable
 */
class condition_variable
{
public:
    explicit condition_variable(std::shared_ptr<io_scheduler> scheduler = nullptr);

    condition_variable(const condition_variable&)            = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    /**
     * Notifies one waiting coroutine
     */
    void notify_one() noexcept;

    /**
     * Notifies all waiting threads
     */
    void notify_all() noexcept;

    /**
     * Suspend the current coroutine until the condition variable is awakened
     * @param lock an lock which must be locked by the calling coroutine
     * @return The task to await until the condition variable is awakened.
     */
    [[nodiscard]] auto wait(scoped_lock& lock) -> task<void>;

    /**
     * Suspend the current coroutine until the condition variable is awakened and predicate becomes true
     * @param lock an lock which must be locked by the calling coroutine
     * @param pred the predicate to check whether the waiting can be completed
     * @return The task to await until the condition variable is awakened and predicate becomes true.
     */
    template<class Predicate>
    [[nodiscard]] auto wait(scoped_lock& lock, Predicate pred) -> task<void>;

    /**
     * Causes the current coroutine to suspend until the condition variable is notified, or the given duration has been
     * elapsed
     * @param lock an lock which must be locked by the calling coroutine
     * @param duration the maximum duration to wait
     * @return The task to await until the condition variable is notified, or the given duration has been elapsed
     */
    template<class Rep, class Period>
    [[nodiscard]] auto
        wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration) -> task<std::cv_status>;

    /**
     * Causes the current coroutine to suspend until the condition variable is notified and predicate becomes true, or
     * the given duration has been elapsed
     * @param lock an lock which must be locked by the calling coroutine
     * @param duration the maximum duration to wait
     * @param pred the predicate to check whether the waiting can be completed
     * @return The task to await until the condition variable is notified and predicate becomes true, or the given
     * duration has been elapsed
     */
    template<class Rep, class Period, class Predicate>
    [[nodiscard]] auto
        wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration, Predicate pred) -> task<bool>;

    /**
     * Causes the current coroutine to suspend until the condition variable is notified, or the given time point has
     * been reached
     * @param lock an lock which must be locked by the calling coroutine
     * @param wakeup the time point where waiting expires
     * @return The task to await until the condition variable is notified, or the given time point has been reached
     */
    template<class Clock, class Duration>
    [[nodiscard]] auto
        wait_until(scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup) -> task<std::cv_status>;

    /**
     * Causes the current coroutine to suspend until the condition variable is notified and predicate becomes true, or
     * the given time point has been reached
     * @param lock an lock which must be locked by the calling coroutine
     * @param wakeup the time point where waiting expires
     * @param pred the predicate to check whether the waiting can be completed
     * @return The task to await until the condition variable is notified and predicate becomes true, or the given time
     * point has been reached
     */
    template<class Clock, class Duration, class Predicate>
    [[nodiscard]] auto wait_until(
        scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup, Predicate pred) -> task<bool>;

    std::shared_ptr<io_scheduler> scheduler() const noexcept;

    void set_scheduler(std::shared_ptr<io_scheduler> scheduler);

    struct wait_operation
    {
        explicit wait_operation(condition_variable& cv);
        ~wait_operation();

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> void;

    private:
        friend class condition_variable;

        condition_variable&          m_condition_variable;
        std::coroutine_handle<>      m_awaiting_coroutine;
        std::atomic<wait_operation*> m_next{nullptr};
    };

private:
    friend struct wait_operation;
    friend class std::lock_guard<condition_variable>;

    /// A scheduler is needed to suspend coroutines and then wake them up upon notification or timeout.
    std::shared_ptr<io_scheduler> m_scheduler;

    /// A list of grabbed internal waiters that are only accessed by the notify'er or task that was cancelled due to
    /// timeout.
    std::atomic<wait_operation*> m_internal_waiters{nullptr};

    /// An atomic-based mutex analog to prevent race conditions between the notify'er and the task being cancelled on
    /// timeout. unlocked == nullptr
    std::atomic<void*> m_lock{nullptr};

    /// Internal unification version of the function for waiting on a @ref coro::condition_variable with a time limit
    [[nodiscard]] auto wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration) -> task<std::cv_status>;

    /// Internal helper function to wait for a condition variable
    [[nodiscard]] auto wait_for_notify() -> wait_operation { return wait_operation{*this}; };

    /// Internal helper function to wait for a condition variable. This is necessary for the scheduler when he schedules
    /// a task with a time limit
    [[nodiscard]] auto wait_task() -> task<bool>;

    /// Lock a mutex m_lock for exclusive operation on @ref m_internal_waiters
    void lock() noexcept;

    /// Unlock a mutex m_lock for exclusive operation on @ref m_internal_waiters
    void unlock() noexcept;

    /// Insert @ref waiter to @ref m_internal_waiters
    void insert_waiter(wait_operation* waiter) noexcept;

    /// Extract @ref waiter from @ref m_internal_waiters
    bool extract_waiter(wait_operation* waiter) noexcept;

    class wait_operation_guard
    {
    public:
        explicit wait_operation_guard(condition_variable* cv) noexcept;
        ~wait_operation_guard();
        operator bool() const noexcept;
        wait_operation* value() const noexcept;
        void            set_value(wait_operation* value) noexcept;

    private:
        condition_variable* m_cv{};
        wait_operation*     m_value{};
    };

    /// Extract one waiter from @ref m_internal_waiters
    wait_operation_guard extract_one();

    /// Extract all waiter from @ref m_internal_waiters
    wait_operation_guard extract_all();
};

template<class Clock, class Duration, class Predicate>
inline auto condition_variable::wait_until(
    scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup, Predicate pred) -> task<bool>
{
    while (!pred())
    {
        if (co_await wait_until(lock, wakeup) == std::cv_status::timeout)
        {
            co_return pred();
        }
    }
    co_return true;
}

template<class Clock, class Duration>
inline auto condition_variable::wait_until(scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup)
    -> task<std::cv_status>
{
    using namespace std::chrono;

    auto msec = duration_cast<milliseconds>(wakeup - Clock::now());

    if (msec.count() <= 0)
    {
        msec = 1ms; // prevent infinity wait
    }

    co_return co_await wait_for_ms(lock, msec);
}

template<class Rep, class Period, class Predicate>
inline auto condition_variable::wait_for(
    scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration, Predicate pred) -> task<bool>
{
    while (!pred())
    {
        if (co_await wait_for(lock, duration) == std::cv_status::timeout)
        {
            co_return pred();
        }
    }
    co_return true;
}

template<class Rep, class Period>
inline auto condition_variable::wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration)
    -> task<std::cv_status>
{
    using namespace std::chrono;

    auto msec = duration_cast<milliseconds>(duration);
    if (msec.count() <= 0)
    {
        // infinity wait
        co_await wait(lock);
        co_return std::cv_status::no_timeout;
    }
    else
    {
        co_return co_await wait_for_ms(lock, msec);
    }
}

template<class Predicate>
inline auto condition_variable::wait(scoped_lock& lock, Predicate pred) -> task<void>
{
    while (!pred())
    {
        co_await wait(lock);
    }
}
} // namespace coro

#pragma once

#include "coro/facade.hpp"
#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
#else
    #include "coro/concepts/executor.hpp"
    #include <condition_variable>
#endif

#include "coro/mutex.hpp"

namespace coro
{

namespace concepts
{
// clang-format off
template<typename strategy_type>
concept cv_strategy_base = requires(strategy_type s)
{
    { s.notify_one() } -> std::same_as<void>;
    { s.notify_all() } -> std::same_as<void>;
    { s.wait_for_notify() } -> coro::concepts::awaiter;
};

template<typename strategy_type>
concept cv_strategy = cv_strategy_base<strategy_type> and requires(strategy_type s, coro::scoped_lock l, std::chrono::milliseconds d)
{
    { s.wait_for_ms(l, d) } -> std::same_as<coro::task<std::cv_status>>;
};
// clang-format on
} // namespace concepts

namespace detail
{

class strategy_base
{
public:
    struct wait_operation
    {
        explicit wait_operation(strategy_base& cv);

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> void;

    private:
        friend class strategy_base;

        strategy_base&               m_strategy;
        std::coroutine_handle<>      m_awaiting_coroutine;
        std::atomic<wait_operation*> m_next{nullptr};
    };

    void notify_one() noexcept;

    /**
     * Notifies and resume one awaiters onto the given executor. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    [[nodiscard]] auto notify_one(executor_type& e) -> task<void>
    {
        while (true)
        {
            auto* current = m_internal_waiters.load(std::memory_order::acquire);
            if (!current)
            {
                break;
            }

            auto* next = current->m_next.load(std::memory_order::acquire);
            if (!m_internal_waiters.compare_exchange_weak(
                    current, next, std::memory_order::release, std::memory_order::acquire))
            {
                continue;
            }

            e.resume(current->m_awaiting_coroutine);
            break;
        }

        co_return;
    }

    void notify_all() noexcept;

    /**
     * Notifies and resumes all awaiters onto the given executor. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    [[nodiscard]] auto notify_all(executor_type& e) -> task<void>
    {
        while (true)
        {
            auto* current = m_internal_waiters.load(std::memory_order::acquire);
            if (!current)
            {
                break;
            }

            if (!m_internal_waiters.compare_exchange_weak(
                    current, nullptr, std::memory_order::release, std::memory_order::acquire))
            {
                continue;
            }

            auto* next         = current->m_next.load(std::memory_order::acquire);
            auto* locked_value = get_locked_value();

            if (next == locked_value)
            {
                // another thread in notify_all() has already taken this waiter
                break;
            }

            if (!current->m_next.compare_exchange_weak(
                    next, locked_value, std::memory_order::release, std::memory_order::acquire))
            {
                continue;
            }

            e.resume(current->m_awaiting_coroutine);

            do
            {
                current = next;
                next    = current->m_next.load(std::memory_order::acquire);
                e.resume(current->m_awaiting_coroutine);
            } while (next);
        }

        co_return;
    }

    /// Internal helper function to wait for a condition variable
    [[nodiscard]] auto wait_for_notify() -> wait_operation;

protected:
    friend struct wait_operation;

    /// A list of grabbed internal waiters that are only accessed by the notify'er
    std::atomic<wait_operation*> m_internal_waiters{nullptr};

    wait_operation* get_locked_value() noexcept;
};

#ifdef LIBCORO_FEATURE_NETWORKING

class strategy_based_on_io_scheduler
{
public:
    explicit strategy_based_on_io_scheduler(
        std::shared_ptr<io_scheduler> io_scheduler = coro::facade::instance()->get_io_scheduler());

    struct wait_operation
    {
        explicit wait_operation(strategy_based_on_io_scheduler& cv);
        ~wait_operation();

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> void;

    private:
        friend class strategy_based_on_io_scheduler;

        strategy_based_on_io_scheduler& m_strategy;
        std::coroutine_handle<>         m_awaiting_coroutine;
        std::atomic<wait_operation*>    m_next{nullptr};
    };

    void notify_one() noexcept;

    void notify_all() noexcept;

    /// Internal helper function to wait for a condition variable
    [[nodiscard]] auto wait_for_notify() -> wait_operation { return wait_operation{*this}; };

    /// Internal unification version of the function for waiting on a coro::condition_variable with a time limit
    [[nodiscard]] auto wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration) -> task<std::cv_status>;

protected:
    friend class std::lock_guard<strategy_based_on_io_scheduler>;
    friend struct wait_operation;

    /// A scheduler is needed to suspend coroutines and then wake them up upon notification or timeout.
    std::weak_ptr<io_scheduler> m_scheduler;

    /// A list of grabbed internal waiters that are only accessed by the notify'er or task that was cancelled due to
    /// timeout.
    std::atomic<wait_operation*> m_internal_waiters{nullptr};

    /// An atomic-based mutex analog to prevent race conditions between the notify'er and the task being cancelled on
    /// timeout. unlocked == nullptr
    std::atomic<void*> m_lock{nullptr};

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
        explicit wait_operation_guard(strategy_based_on_io_scheduler* cv) noexcept;
        ~wait_operation_guard();
        operator bool() const noexcept;
        wait_operation* value() const noexcept;
        void            set_value(wait_operation* value) noexcept;

    private:
        strategy_based_on_io_scheduler* m_cv{};
        wait_operation*                 m_value{};
    };

    /// Extract one waiter from @ref m_internal_waiters
    wait_operation_guard extract_one();

    /// Extract all waiter from @ref m_internal_waiters
    wait_operation_guard extract_all();

    /// Internal helper function to wait for a condition variable. This is necessary for the scheduler when he schedules
    /// a task with a time limit
    [[nodiscard]] auto wait_task() -> task<bool>;
};

using default_strategy = strategy_based_on_io_scheduler;
#else
using default_strategy = strategy_base;
#endif

} // namespace detail

/**
 * The coro::condition_variable is a thread safe async tool used with a coro::mutex (coro::scoped_lock)
 * to suspend one or more coro::task until another coro::task both modifies a shared variable
 *  (the condition) and notifies the coro::condition_variable
 */
template<concepts::cv_strategy_base Strategy>
class condition_variable_base : public Strategy
{
public:
    condition_variable_base() = default;

    template<typename... Args>
    explicit condition_variable_base(Args&&... args) : Strategy(std::forward<Args>(args)...)
    {
    }

    condition_variable_base(const condition_variable_base&)            = delete;
    condition_variable_base& operator=(const condition_variable_base&) = delete;

    /**
     * Notifies one waiting coroutine
     */
    using Strategy::notify_one;

    /**
     * Notifies all waiting threads
     */
    using Strategy::notify_all;

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
        requires concepts::cv_strategy<Strategy>
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
        requires concepts::cv_strategy<Strategy>
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
        requires concepts::cv_strategy<Strategy>
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
        requires concepts::cv_strategy<Strategy>
    [[nodiscard]] auto wait_until(
        scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup, Predicate pred) -> task<bool>;
};

template<concepts::cv_strategy_base Strategy>
inline auto condition_variable_base<Strategy>::wait(scoped_lock& lock) -> task<void>
{
    auto mtx = lock.mutex();
    lock.unlock();

    co_await Strategy::wait_for_notify();

    auto ulock = co_await mtx->lock();
    lock       = std::move(ulock);
    co_return;
}

template<concepts::cv_strategy_base Strategy>
template<class Predicate>
inline auto condition_variable_base<Strategy>::wait(scoped_lock& lock, Predicate pred) -> task<void>
{
    while (!pred())
    {
        co_await wait(lock);
    }
}

template<concepts::cv_strategy_base Strategy>
template<class Clock, class Duration, class Predicate>
    requires concepts::cv_strategy<Strategy>
inline auto condition_variable_base<Strategy>::wait_until(
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

template<concepts::cv_strategy_base Strategy>
template<class Clock, class Duration>
    requires concepts::cv_strategy<Strategy>
inline auto condition_variable_base<Strategy>::wait_until(
    scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup) -> task<std::cv_status>
{
    using namespace std::chrono;

    auto msec = duration_cast<milliseconds>(wakeup - Clock::now());

    if (msec.count() <= 0)
    {
        msec = 1ms; // prevent infinity wait
    }

    co_return co_await Strategy::wait_for_ms(lock, msec);
}

template<concepts::cv_strategy_base Strategy>
template<class Rep, class Period, class Predicate>
    requires concepts::cv_strategy<Strategy>
inline auto condition_variable_base<Strategy>::wait_for(
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

template<concepts::cv_strategy_base Strategy>
template<class Rep, class Period>
    requires concepts::cv_strategy<Strategy>
inline auto condition_variable_base<Strategy>::wait_for(
    scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration) -> task<std::cv_status>
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
        co_return co_await Strategy::wait_for_ms(lock, msec);
    }
}

using condition_variable = condition_variable_base<detail::default_strategy>;

} // namespace coro

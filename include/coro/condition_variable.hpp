#pragma once

#include "coro/default_executor.hpp"
#ifdef LIBCORO_FEATURE_NETWORKING
    #include "coro/io_scheduler.hpp"
#else
    #include "coro/concepts/executor.hpp"
    #include <condition_variable>
#endif

#include "coro/detail/lockfree_object_pool.hpp"
#include "coro/mutex.hpp"

namespace coro
{

namespace concepts
{
// clang-format off


/**
 * Concept of basic capabilities condition_variable
 */
template<typename strategy_type>
concept cv_strategy_base = requires(strategy_type s, scoped_lock& l)
{
    { s.wait(l) }-> std::same_as<task<void>>;
    { s.notify_one() } -> std::same_as<void>;
    { s.notify_all() } -> std::same_as<void>;
};

/**
 * Concept of full capabilities condition_variable
 */
template<typename strategy_type>
concept cv_strategy = cv_strategy_base<strategy_type> and requires(strategy_type s, coro::scoped_lock l, std::chrono::milliseconds d)
{
    { s.wait_for_ms(l, d) } -> std::same_as<coro::task<std::cv_status>>;
};
// clang-format on
} // namespace concepts

namespace detail
{

/**
 * The strategy implementing basic features of condition_variable, such as wait(), notify_one(), notify_all(). Does not
 * require LIBCORO_FEATURE_NETWORKING for its operation
 */
class strategy_base
{
public:
    template<concepts::executor executor_type>
    struct wait_operation_with_executor;

    struct wait_operation
    {
        explicit wait_operation(strategy_base& strategy, scoped_lock&& lock);

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> void {}

    protected:
        friend class strategy_base;

        template<concepts::executor executor_type>
        friend struct wait_operation_with_executor;

        strategy_base&          m_strategy;
        std::coroutine_handle<> m_awaiting_coroutine;
        scoped_lock             m_lock;
    };

    template<concepts::executor executor_type>
    struct wait_operation_with_executor
    {
        explicit wait_operation_with_executor(strategy_base& strategy, scoped_lock&& lock, executor_type& e)
            : m_waiter(strategy, std::move(lock)),
              m_executor(e)
        {
        }

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            m_waiter.m_awaiting_coroutine = awaiting_coroutine;
            m_waiter.m_strategy.m_internal_waiters.push(&m_waiter);
            m_waiter.m_lock.unlock(m_executor);
            return true;
        }
        auto await_resume() noexcept -> void {}

    private:
        wait_operation m_waiter;
        executor_type& m_executor;
    };

    /**
     * Suspend the current coroutine until the condition variable is awakened
     * @param lock an lock which must be locked by the calling coroutine
     * @return The task to await until the condition variable is awakened.
     */
    auto wait(scoped_lock& lock) -> task<void>;

    /**
     * Suspend the current coroutine until the condition variable is awakened. This will distribute
     * the waiters across the executor's threads.
     * @param lock an lock which must be locked by the calling coroutine
     * @return The task to await until the condition variable is awakened.
     */
    template<concepts::executor executor_type>
    auto wait(scoped_lock& lock, executor_type& e) -> task<void>
    {
        auto mtx = lock.mutex();

        co_await wait_for_notify(std::move(lock), e);

        auto ulock = co_await mtx->lock();
        lock       = std::move(ulock);
        co_return;
    }

    /**
     * Notifies and resumes one waiter immediately at the moment of the call, the calling coroutine will be forced to
     * wait for the switching of the awakened coroutine
     */
    void notify_one() noexcept;

    /**
     * Notifies and resume one awaiters onto the given executor. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    void notify_one(executor_type& e)
    {
        if (auto waiter = m_internal_waiters.pop().value_or(nullptr))
        {
            e.resume(waiter->m_awaiting_coroutine);
        }
    }

    /**
     * Notifies and resumes all awaiters immediately at the moment of the call, the calling coroutine will be forced to
     * wait for the switching of all awakened coroutines
     */
    void notify_all() noexcept;

    /**
     * Notifies and resumes all awaiters onto the given executor. This will distribute
     * the waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    void notify_all(executor_type& e)
    {
        while (auto waiter = m_internal_waiters.pop().value_or(nullptr))
        {
            e.resume(waiter->m_awaiting_coroutine);
        }
    }

    /// Internal helper function to wait for a condition variable
    [[nodiscard]] auto wait_for_notify(scoped_lock&& lock) -> wait_operation
    {
        return wait_operation{*this, std::move(lock)};
    };

    /**
     * Internal helper function to wait for a condition variable. This will distribute coro::mutex
     * waiters across the executor's threads.
     */
    template<concepts::executor executor_type>
    [[nodiscard]] auto wait_for_notify(scoped_lock&& lock, executor_type& e) -> wait_operation
    {
        return wait_operation_with_executor{*this, std::move(lock), e};
    };

protected:
    friend struct wait_operation;

    /// A queue of grabbed internal waiters that are only accessed by the notify'er the wait'er
    coro::detail::lockfree_queue_based_on_pool<wait_operation*> m_internal_waiters;
};

#ifdef LIBCORO_FEATURE_NETWORKING

/**
 * The strategy fully implements all the capabilities of condition_variable, including such as wait_for(), wait_until().
 * Requires LIBCORO_FEATURE_NETWORKING for its operation.
 */
class strategy_based_on_io_scheduler
{
protected:
    struct wait_operation_link;
    using wait_operation_link_unique_ptr =
        std::unique_ptr<wait_operation_link, std::function<void(wait_operation_link*)>>;

public:
    explicit strategy_based_on_io_scheduler(
        std::shared_ptr<io_scheduler> io_scheduler = coro::default_executor::io_executor());

    ~strategy_based_on_io_scheduler();

    struct wait_operation
    {
        explicit wait_operation(strategy_based_on_io_scheduler& strategy, scoped_lock&& lock);
        ~wait_operation();

        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> void {};

    private:
        friend class strategy_based_on_io_scheduler;

        strategy_based_on_io_scheduler&   m_strategy;
        std::coroutine_handle<>           m_awaiting_coroutine;
        std::atomic<wait_operation_link*> m_link{nullptr};
        scoped_lock                       m_lock;
    };

    auto wait(scoped_lock& lock) -> task<void>;

    void notify_one() noexcept;

    void notify_all() noexcept;

    /// Internal helper function to wait for a condition variable
    [[nodiscard]] auto wait_for_notify(scoped_lock&& lock) -> wait_operation
    {
        return wait_operation{*this, std::move(lock)};
    };

    /// Internal unification version of the function for waiting on a coro::condition_variable with a time limit
    [[nodiscard]] auto wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration) -> task<std::cv_status>;

protected:
    friend struct wait_operation;

    struct wait_operation_link
    {
        std::atomic<wait_operation*> waiter{nullptr};
    };

    /// A scheduler is needed to suspend coroutines and then wake them up upon notification or timeout.
    std::weak_ptr<io_scheduler> m_scheduler;

    /// A queue of grabbed internal waiters that are only accessed by the notify'er the wait'er
    coro::detail::lockfree_queue_based_on_pool<wait_operation_link*> m_internal_waiters;

    /// A object pool of free wait_operation_link
    coro::detail::lockfree_object_pool<wait_operation_link> m_free_links;

    /// Insert @ref waiter to @ref m_internal_waiters
    void insert_waiter(wait_operation* waiter) noexcept;

    /// Extract @ref waiter from @ref m_internal_waiters
    void extract_waiter(wait_operation* waiter) noexcept;

    /// Extract one waiter from @ref m_internal_waiters
    wait_operation_link_unique_ptr extract_one();

    /**
     * Internal helper function to wait for a condition variable. This is necessary for wait_for_ms
     * @param wo a wait operation, which is also used in timeout_task for extract_waiter
     * @param stop_source a stop event to cancel coroutine
     */
    [[nodiscard]] auto wait_task(std::shared_ptr<wait_operation> wo, std::stop_source stop_source) -> task<bool>;

    /**
     * Internal helper function to wait for timeout and extract_waiter. This is necessary for wait_for_ms
     * @param wo a wait operation, which is used for extract_waiter
     * @param timeout the maximum duration to wait
     * @param stop_source a stop event to cancel coroutine
     */
    [[nodiscard]] auto timeout_task(
        std::shared_ptr<wait_operation> wo,
        std::chrono::milliseconds       timeout,
        std::stop_source                stop_source) -> task<timeout_status>;
};
#endif

/**
 * You can set a custom default strategy for the coro::condition_variable
 */
#ifdef LIBCORO_CONDITION_VARIABLE_DEFAULT_STRATEGY
using default_strategy = LIBCORO_CONDITION_VARIABLE_DEFAULT_STRATEGY;
#else
    #ifdef LIBCORO_FEATURE_NETWORKING
using default_strategy = strategy_based_on_io_scheduler;
    #else  // LIBCORO_FEATURE_NETWORKING
using default_strategy = strategy_base;
    #endif // LIBCORO_FEATURE_NETWORKING
#endif     // LIBCORO_CONDITION_VARIABLE_DEFAULT_STRATEGY
} // namespace detail

/**
 * The coro::condition_variable_base is a thread safe async tool used with a coro::mutex (coro::scoped_lock)
 * to suspend one or more coro::task until another coro::task both modifies a shared variable
 *  (the condition) and notifies the coro::condition_variable_base
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
     * Notifies all waiting coroutines
     */
    using Strategy::notify_all;

    /**
     * Suspend the current coroutine until the condition variable is awakened
     * @param lock an lock which must be locked by the calling coroutine
     * @return The task to await until the condition variable is awakened.
     */
    using Strategy::wait;

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

/**
 * this is coro::condition_variable_base with default strategy parameter (coro::detail::default_strategy)
 */
using condition_variable = condition_variable_base<detail::default_strategy>;

} // namespace coro

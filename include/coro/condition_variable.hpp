#pragma once

#include "coro/io_scheduler.hpp"
#include "coro/mutex.hpp"

namespace coro
{

class condition_variable
{
public:
    explicit condition_variable(std::shared_ptr<io_scheduler> scheduler = nullptr);

    condition_variable(const condition_variable&)            = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    void notify_one() noexcept;

    void notify_all() noexcept;

    auto wait(scoped_lock& lock) -> task<void>;

    template<class Predicate>
    auto wait(scoped_lock& lock, Predicate pred) -> task<void>;

    template<class Rep, class Period>
    auto wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration) -> task<std::cv_status>;

    template<class Rep, class Period, class Predicate>
    auto wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration, Predicate pred) -> task<bool>;

    template<class Clock, class Duration>
    auto wait_until(scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup) -> task<std::cv_status>;

    template<class Clock, class Duration, class Predicate>
    auto wait_until(scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup, Predicate pred)
        -> task<bool>;

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

    std::shared_ptr<io_scheduler> m_scheduler;
    std::atomic<wait_operation*>  m_internal_waiters{nullptr};
    std::atomic<void*>            m_lock{nullptr};

    auto wait_for_ms(scoped_lock& lock, const std::chrono::milliseconds duration = {}) -> task<std::cv_status>;

    [[nodiscard]] auto wait_for_notify() -> wait_operation { return wait_operation{*this}; };

    [[nodiscard]] auto wait_task(condition_variable* cv) -> task<bool>;

    void lock(void* ptr);
    void unlock();
    void insert_waiter(wait_operation* waiter);
    bool extract_waiter(wait_operation* waiter);

    struct wait_operation_guard
    {
        condition_variable* cv{};
        wait_operation*     value{};

        explicit wait_operation_guard(condition_variable* cv, wait_operation* value = nullptr);
        wait_operation_guard(wait_operation_guard&&)            = default;
        wait_operation_guard& operator=(wait_operation_guard&&) = default;
        ~wait_operation_guard();

        wait_operation_guard(const wait_operation_guard&)            = delete;
        wait_operation_guard& operator=(const wait_operation_guard&) = delete;

        operator bool() const;
    };

    wait_operation_guard extract_one();
    wait_operation_guard extract_all();
};

template<class Clock, class Duration, class Predicate>
inline auto condition_variable::wait_until(
    scoped_lock& lock, const std::chrono::time_point<Clock, Duration>& wakeup, Predicate pred) -> task<bool>
{
    while (!pred())
    {
        if (co_await wait_until(lock, wakeup) == std::cv_status::timeout)
            co_return pred();
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
        msec = 1ms; // prevent infinity wait

    co_return co_await wait_for_ms(lock, msec);
}

template<class Rep, class Period, class Predicate>
inline auto condition_variable::wait_for(
    scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration, Predicate pred) -> task<bool>
{
    while (!pred())
    {
        if (co_await wait_for(lock, duration) == std::cv_status::timeout)
            co_return pred();
    }
    co_return true;
}

template<class Rep, class Period>
inline auto condition_variable::wait_for(scoped_lock& lock, const std::chrono::duration<Rep, Period>& duration)
    -> task<std::cv_status>
{
    using namespace std::chrono;

    auto msec = std::max(duration_cast<milliseconds>(duration), 0ms);
    co_return co_await wait_for_ms(lock, msec);
}

template<class Predicate>
inline auto condition_variable::wait(scoped_lock& lock, Predicate pred) -> task<void>
{
    while (!pred())
        co_await wait(lock);
}
} // namespace coro

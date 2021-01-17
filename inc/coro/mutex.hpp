#pragma once

#include <atomic>
#include <coroutine>
#include <deque>
#include <mutex>

namespace coro
{
class mutex
{
public:
    struct scoped_lock
    {
        friend class mutex;

        scoped_lock(mutex& m) : m_mutex(m) {}
        ~scoped_lock() { m_mutex.unlock(); }

        mutex& m_mutex;
    };

    struct awaiter
    {
        awaiter(mutex& m) noexcept : m_mutex(m) {}

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;
        auto await_resume() noexcept -> scoped_lock;

        mutex&                  m_mutex;
        std::coroutine_handle<> m_awaiting_coroutine;
    };

    explicit mutex() noexcept = default;
    ~mutex()                  = default;

    mutex(const mutex&) = delete;
    mutex(mutex&&)      = delete;
    auto operator=(const mutex&) -> mutex& = delete;
    auto operator=(mutex&&) -> mutex& = delete;

    auto lock() -> awaiter;
    auto try_lock() -> bool;
    auto unlock() -> void;

private:
    friend class scoped_lock;

    std::atomic<bool>    m_state{false};
    std::mutex           m_waiter_mutex{};
    std::deque<awaiter*> m_waiter_list{};
};

} // namespace coro

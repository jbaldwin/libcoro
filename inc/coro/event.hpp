#pragma once

#include "coro/concepts/executor.hpp"

#include <atomic>
#include <coroutine>

namespace coro
{
/**
 * Event is a manully triggered thread safe signal that can be co_await()'ed by multiple awaiters.
 * Each awaiter should co_await the event and upon the event being set each awaiter will have their
 * coroutine resumed.
 *
 * The event can be manually reset to the un-set state to be re-used.
 * \code
t1: coro::event e;
...
t2: func(coro::event& e) { ... co_await e; ... }
...
t1: do_work();
t1: e.set();
...
t2: resume()
 * \endcode
 */
class event
{
public:
    struct awaiter
    {
        /**
         * @param e The event to wait for it to be set.
         */
        awaiter(const event& e) noexcept : m_event(e) {}

        /**
         * @return True if the event is already set, otherwise false to suspend this coroutine.
         */
        auto await_ready() const noexcept -> bool { return m_event.is_set(); }

        /**
         * Adds this coroutine to the list of awaiters in a thread safe fashion.  If the event
         * is set while attempting to add this coroutine to the awaiters then this will return false
         * to resume execution immediately.
         * @return False if the event is already set, otherwise true to suspend this coroutine.
         */
        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool;

        /**
         * Nothing to do on resume.
         */
        auto await_resume() noexcept {}

        /// Refernce to the event that this awaiter is waiting on.
        const event& m_event;
        /// The awaiting continuation coroutine handle.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// The next awaiter in line for this event, nullptr if this is the end.
        awaiter* m_next{nullptr};
    };

    /**
     * Creates an event with the given initial state of being set or not set.
     * @param initially_set By default all events start as not set, but if needed this parameter can
     *                      set the event to already be triggered.
     */
    explicit event(bool initially_set = false) noexcept;
    ~event() = default;

    event(const event&) = delete;
    event(event&&)      = delete;
    auto operator=(const event&) -> event& = delete;
    auto operator=(event&&) -> event& = delete;

    /**
     * @return True if this event is currently in the set state.
     */
    auto is_set() const noexcept -> bool { return m_state.load(std::memory_order_acquire) == this; }

    /**
     * Sets this event and resumes all awaiters.  Note that all waiters will be resumed onto this
     * thread of execution.
     */
    auto set() noexcept -> void;

    /**
     * Sets this event and resumes all awaiters onto the given thread pool.  This will distribute
     * the waiters across the thread pools threads.
     */
    template<concepts::executor executor_type>
    auto set(executor_type& e) noexcept -> void
    {
        void* old_value = m_state.exchange(this, std::memory_order::acq_rel);
        if (old_value != this)
        {
            auto* waiters = static_cast<awaiter*>(old_value);
            while (waiters != nullptr)
            {
                auto* next = waiters->m_next;
                e.resume(waiters->m_awaiting_coroutine);
                waiters = next;
            }
        }
    }

    /**
     * @return An awaiter struct to suspend and resume this coroutine for when the event is set.
     */
    auto operator co_await() const noexcept -> awaiter { return awaiter(*this); }

    /**
     * Resets the event from set to not set so it can be re-used.  If the event is not currently
     * set then this function has no effect.
     */
    auto reset() noexcept -> void;

protected:
    /// For access to m_state.
    friend struct awaiter;
    /// The state of the event, nullptr is not set with zero awaiters.  Set to an awaiter* there are
    /// coroutines awaiting the event to be set, and set to this the event has triggered.
    /// 1) nullptr == not set
    /// 2) awaiter* == linked list of awaiters waiting for the event to trigger.
    /// 3) this == The event is triggered and all awaiters are resumed.
    mutable std::atomic<void*> m_state;
};

} // namespace coro

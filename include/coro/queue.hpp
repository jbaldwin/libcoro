#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/expected.hpp"
#include "coro/mutex.hpp"
#include "coro/sync_wait.hpp"
#include "coro/task.hpp"

#include <atomic>
#include <queue>

namespace coro
{

enum class queue_produce_result
{
    /**
     * @brief The item was successfully produced.
     */
    produced,
    /**
     * @brief The queue is shutting down or stopped, no more items are allowed to be produced.
     */
    stopped
};

enum class queue_consume_result
{
    /**
     * @brief The queue has shut down/stopped and the user should stop calling pop().
     */
    stopped,

    /**
     * @brief try_pop() failed to acquire the lock.
     */
    try_lock_failure,

    /**
     * @brief try_pop() acquired the lock but there are no items in the queue.
     */
    empty,
};

/**
 * @brief An unbounded queue. If the queue is empty and there are waiters to consume then
 *        there are no allocations and the coroutine context will simply be passed to the
 *        waiter. If there are no waiters the item being produced will be placed into the
 *        queue.
 *
 * @tparam element_type The type of items being produced and consumed.
 */
template<typename element_type>
class queue
{
private:
    enum running_state_t
    {
        running,
        draining,
        stopped,
    };

public:
    struct awaiter
    {
        explicit awaiter(queue<element_type>& q) noexcept : m_queue(q) {}

        auto await_ready() noexcept -> bool
        {
            // This awaiter is ready when it has actually acquired an element or it is shutting down.
            if (m_queue.m_running_state.load(std::memory_order::acquire) == running_state_t::stopped)
            {
                m_queue.m_mutex.unlock();
                return true; // await_resume with stopped
            }

            // If we have items return it.
            if (!m_queue.empty())
            {
                if constexpr (std::is_move_constructible_v<element_type>)
                {
                    m_element = std::move(m_queue.m_elements.front());
                }
                else
                {
                    m_element = m_queue.m_elements.front();
                }

                m_queue.m_elements.pop();
                m_queue.m_mutex.unlock();
                return true;
            }

            // Nothing available suspend, mutex will be unlocked in await_suspend.
            return false;
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // No element is ready, put ourselves on the waiter list and suspend.
            this->m_next         = m_queue.m_waiters;
            m_queue.m_waiters    = this;
            m_awaiting_coroutine = awaiting_coroutine;
            m_queue.m_mutex.unlock();
            return true;
        }

        [[nodiscard]] auto await_resume() noexcept -> expected<element_type, queue_consume_result>
        {
            if (m_element.has_value())
            {
                if constexpr (std::is_move_constructible_v<element_type>)
                {
                    return std::move(m_element.value());
                }
                else
                {
                    return m_element.value();
                }
            }
            else
            {
                // If we don't have an item the queue has stopped, the prior functions will have checked the state.
                return unexpected<queue_consume_result>(queue_consume_result::stopped);
            }
        }

        std::optional<element_type> m_element{std::nullopt};
        queue&                      m_queue;
        std::coroutine_handle<>     m_awaiting_coroutine{nullptr};
        awaiter*                    m_next{nullptr};
    };

    queue() {}
    ~queue()
    {
        coro::sync_wait(shutdown());
    }

    queue(const queue&)  = delete;
    queue(queue&& other) = delete;

    auto operator=(const queue&) -> queue&  = delete;
    auto operator=(queue&& other) -> queue& = delete;

    /**
     * @brief Determines if the queue is empty.
     *
     * @return true If the queue is empty.
     * @return false If the queue is not empty.
     */
    auto empty() const -> bool { return size() == 0; }

    /**
     * @brief Gets the number of elements in the queue.
     *
     * @return std::size_t The number of elements in the queue.
     */
    auto size() const -> std::size_t
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_elements.size();
    }

    /**
     * @brief Pushes the element into the queue. If the queue is empty and there are waiters
     *        then the element will be processed immediately by transfering the coroutine task
     *        context to the waiter.
     *
     * @param element The element being produced.
     * @return coro::task<queue_produce_result>
     */
    auto push(const element_type& element) -> coro::task<queue_produce_result>
    {
        // The general idea is to see if anyone is waiting, and if so directly transfer the element
        // to that waiter. If there is nobody waiting then move the element into the queue.
        auto lock = co_await m_mutex.scoped_lock();

        if (m_running_state.load(std::memory_order::acquire) != running_state_t::running)
        {
            co_return queue_produce_result::stopped;
        }

        // assert(m_element.empty())
        if (m_waiters != nullptr)
        {
            auto* waiter = std::exchange(m_waiters, m_waiters->m_next);
            lock.unlock();

            // Transfer the element directly to the awaiter.
            waiter->m_element = element;
            waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            m_elements.push(element);
        }

        co_return queue_produce_result::produced;
    }

    /**
     * @brief Pushes the element into the queue. If the queue is empty and there are waiters
     *        then the element will be processed immediately by transfering the coroutine task
     *        context to the waiter.
     *
     * @param element The element being produced.
     * @return coro::task<queue_produce_result>
     */
    auto push(element_type&& element) -> coro::task<queue_produce_result>
    {
        auto lock = co_await m_mutex.scoped_lock();

        if (m_running_state.load(std::memory_order::acquire) != running_state_t::running)
        {
            co_return queue_produce_result::stopped;
        }

        if (m_waiters != nullptr)
        {
            auto* waiter = std::exchange(m_waiters, m_waiters->m_next);
            lock.unlock();

            // Transfer the element directly to the awaiter.
            waiter->m_element = std::move(element);
            waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            m_elements.push(std::move(element));
        }

        co_return queue_produce_result::produced;
    }

    /**
     * @brief Emplaces an element into the queue. Has the same behavior as push if the queue
     *        is empty and has waiters.
     *
     * @param args The element's constructor argument types and values.
     * @return coro::task<queue_produce_result>
     */
    template<typename... args_type>
    auto emplace(args_type&&... args) -> coro::task<queue_produce_result>
    {
        auto lock = co_await m_mutex.scoped_lock();

        if (m_running_state.load(std::memory_order::acquire) != running_state_t::running)
        {
            co_return queue_produce_result::stopped;
        }

        if (m_waiters != nullptr)
        {
            auto* waiter = std::exchange(m_waiters, m_waiters->m_next);
            lock.unlock();

            waiter->m_element.emplace(std::forward<args_type>(args)...);
            waiter->m_awaiting_coroutine.resume();
        }
        else
        {
            m_elements.emplace(std::forward<args_type>(args)...);
        }

        co_return queue_produce_result::produced;
    }

    /**
     * @brief Pops the head element of the queue if available, or waits for one to be available.
     *
     * @return awaiter A waiter task that upon co_await complete returns an element or the queue
     *                 status that it is shut down.
     */
    [[nodiscard]] auto pop() -> coro::task<expected<element_type, queue_consume_result>>
    {
        co_await m_mutex.lock();
        co_return co_await awaiter{*this};
    }

    /**
     * @brief Tries to pop the head element of the queue if available. This can fail if it cannot
     *        acquire the lock via `coro::mutex::try_lock()` or if there are no elements available.
     *        Does not block.
     *
     * @return expected<element_type, queue_consume_result> The head element if the lock was acquired
     *         and an element is available.
     *         queue_consume_result::stopped if the queue has been shutdown.
     *         queue_consume_result::empty if lock was acquired but the queue is empty.
     *         queue_consume_result::try_lock_failure if the queue is in use and the lock could not be acquired.
     */
    [[nodiscard]] auto try_pop() -> expected<element_type, queue_consume_result>
    {
        if (m_mutex.try_lock())
        {
            // Capture mutex into a scoped lock to manage unlocking correctly.
            coro::scoped_lock lk{m_mutex};

            // Return if stopped.
            if (m_running_state.load(std::memory_order::acquire) == running_state_t::stopped)
            {
                return unexpected<queue_consume_result>(queue_consume_result::stopped);
            }

            // Return if empty.
            if (empty())
            {
                return unexpected<queue_consume_result>(queue_consume_result::empty);
            }

            expected<element_type, queue_consume_result> value;
            if constexpr (std::is_move_constructible_v<element_type>)
            {
                value = std::move(m_elements.front());
            }
            else
            {
                value = m_elements.front();
            }

            m_elements.pop();
            return value;
        }

        return unexpected<queue_consume_result>(queue_consume_result::try_lock_failure);
    }

    /**
     * @brief Shuts down the queue immediately discarding any elements that haven't been processed.
     *
     * @return coro::task<void>
     */
    auto shutdown() -> coro::task<void>
    {
        auto expected = m_running_state.load(std::memory_order::acquire);
        if (expected == running_state_t::stopped)
        {
            co_return;
        }

        // We use the lock to guarantee the m_running_state has propagated.
        auto lk = co_await m_mutex.scoped_lock();
        if (!m_running_state.compare_exchange_strong(
                expected, running_state_t::stopped, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }

        auto* waiters = m_waiters;
        m_waiters = nullptr;
        lk.unlock();
        while (waiters != nullptr)
        {
            auto* next = waiters->m_next;
            waiters->m_awaiting_coroutine.resume();
            waiters = next;
        }
    }

    /**
     * @brief Shuts down the queue but waits for it to be drained so all elements are processed.
     *        Will yield on the given executor between checking if the queue is empty so the tasks
     *        can be processed.
     *
     * @tparam executor_t The executor type.
     * @param e The executor to yield this task to wait for elements to be processed.
     * @return coro::task<void>
     */
    template<coro::concepts::executor executor_type>
    auto shutdown_drain(std::unique_ptr<executor_type>& e) -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        auto expected = running_state_t::running;
        if (!m_running_state.compare_exchange_strong(
                expected, running_state_t::draining, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }
        lk.unlock();

        while (!empty() && m_running_state.load(std::memory_order::acquire) == running_state_t::draining)
        {
            co_await e->yield();
        }

        co_return co_await shutdown();
    }

    /**
     * Returns true if shutdown() or shutdown_drain() have been called on this coro::queue.
     * @return True if the coro::queue has been shutdown.
     */
    [[nodiscard]] auto is_shutdown() const -> bool { return m_running_state.load(std::memory_order::acquire) != running_state_t::running; }

private:
    friend awaiter;
    /// @brief The list of pop() awaiters.
    awaiter* m_waiters{nullptr};
    /// @brief Mutex for properly maintaining the queue.
    coro::mutex m_mutex{};
    /// @brief The underlying queue data structure.
    std::queue<element_type> m_elements{};
    /// @brief The current running state of the queue.
    std::atomic<running_state_t> m_running_state{running_state_t::running};
};

} // namespace coro

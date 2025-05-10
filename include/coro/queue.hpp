#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/expected.hpp"
#include "coro/sync_wait.hpp"

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
    queue_stopped
};

enum class queue_consume_result
{
    /**
     * @brief The queue has shut down/stopped and the user should stop calling pop().
     */
    queue_stopped
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
public:
    struct awaiter
    {
        explicit awaiter(queue<element_type>& q) noexcept : m_queue(q) {}

        /**
         * @brief Acquires the coro::queue lock.
         *
         * @return coro::task<scoped_lock>
         */
        auto make_acquire_lock_task() -> coro::task<scoped_lock> { co_return co_await m_queue.m_mutex.lock(); }

        auto await_ready() noexcept -> bool
        {
            // This awaiter is ready when it has actually acquired an element or it is shutting down.
            if (m_queue.m_stopped.load(std::memory_order::acquire))
            {
                return false;
            }

            auto lock = coro::sync_wait(make_acquire_lock_task());
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
                return true;
            }

            return false;
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // Don't suspend if the stop signal has been set.
            if (m_queue.m_stopped.load(std::memory_order::acquire))
            {
                return false;
            }

            auto lock = coro::sync_wait(make_acquire_lock_task());
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
                return false;
            }

            // No element is ready, put ourselves on the waiter list and suspend.
            this->m_next         = m_queue.m_waiters;
            m_queue.m_waiters    = this;
            m_awaiting_coroutine = awaiting_coroutine;

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
                return unexpected<queue_consume_result>(queue_consume_result::queue_stopped);
            }
        }

        std::optional<element_type> m_element{std::nullopt};
        queue&                      m_queue;
        std::coroutine_handle<>     m_awaiting_coroutine{nullptr};
        /// The next awaiter in line for this queue, nullptr if this is the end.
        awaiter* m_next{nullptr};
    };

    queue() {}
    ~queue() {}

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
        if (m_shutting_down.load(std::memory_order::acquire))
        {
            co_return queue_produce_result::queue_stopped;
        }

        // The general idea is to see if anyone is waiting, and if so directly transfer the element
        // to that waiter. If there is nobody waiting then move the element into the queue.
        auto lock = co_await m_mutex.lock();

        if (m_waiters != nullptr)
        {
            awaiter* waiter = m_waiters;
            m_waiters       = m_waiters->m_next;
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
        if (m_shutting_down.load(std::memory_order::acquire))
        {
            co_return queue_produce_result::queue_stopped;
        }

        auto lock = co_await m_mutex.lock();

        if (m_waiters != nullptr)
        {
            awaiter* waiter = m_waiters;
            m_waiters       = m_waiters->m_next;
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
        if (m_shutting_down.load(std::memory_order::acquire))
        {
            co_return queue_produce_result::queue_stopped;
        }

        auto lock = co_await m_mutex.lock();

        if (m_waiters != nullptr)
        {
            awaiter* waiter = m_waiters;
            m_waiters       = m_waiters->m_next;
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
    [[nodiscard]] auto pop() -> awaiter { return awaiter{*this}; }

    /**
     * @brief Shuts down the queue immediately discarding any elements that haven't been processed.
     *
     * @return coro::task<void>
     */
    auto shutdown_notify_waiters() -> coro::task<void>
    {
        auto expected = false;
        if (!m_shutting_down.compare_exchange_strong(
                expected, true, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }

        // Since this isn't draining just let the awaiters know we're stopped.
        m_stopped.exchange(true, std::memory_order::release);

        auto lock = co_await m_mutex.lock();
        while (m_waiters != nullptr)
        {
            auto* to_resume = m_waiters;
            m_waiters       = m_waiters->m_next;

            lock.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lock = co_await m_mutex.lock();
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
    template<coro::concepts::executor executor_t>
    auto shutdown_notify_waiters_drain(executor_t& e) -> coro::task<void>
    {
        auto expected = false;
        if (!m_shutting_down.compare_exchange_strong(
                expected, true, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }

        while (!empty())
        {
            co_await e.yield();
        }

        // Now that the queue is drained let all the awaiters know that we're stopped.
        m_stopped.exchange(true, std::memory_order::release);

        auto lock = co_await m_mutex.lock();
        while (m_waiters != nullptr)
        {
            auto* to_resume = m_waiters;
            m_waiters       = m_waiters->m_next;

            lock.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lock = co_await m_mutex.lock();
        }
    }

private:
    friend awaiter;
    /// The list of pop() awaiters.
    awaiter* m_waiters{nullptr};
    /// Mutex for properly maintaining the queue.
    coro::mutex m_mutex{};
    /// The underlying queue data structure.
    std::queue<element_type> m_elements{};
    /// Has the shutdown process begun?
    std::atomic<bool> m_shutting_down{false};
    /// Has this queue been shutdown?
    std::atomic<bool> m_stopped{false};
};

} // namespace coro

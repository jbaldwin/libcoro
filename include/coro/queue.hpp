#pragma once

#include "coro/expected.hpp"

#include <mutex>
#include <queue>

namespace coro
{

/// @brief Enum used for push() expected result.
enum class queue_produce_result
{
    produced,
    queue_stopped
};

/// @brief Enum used for pop() expected result when the queue is shutting down.
enum class queue_consume_result
{
    queue_stopped
};

/// @brief
/// @tparam element_type
template<typename element_type>
class queue
{
public:
    struct awaiter
    {
        explicit awaiter(queue<element_type>& q) noexcept : m_queue(q) {}

        auto await_ready() noexcept -> bool
        {
            // This awaiter is ready when it has actually acquired an element or it is shutting down.
            if (m_queue.m_stopped.load(std::memory_order::acquire))
            {
                return false;
            }

            std::scoped_lock lock{m_queue.m_mutex};
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

            std::scoped_lock lock{m_queue.m_mutex};
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
            if (m_queue.m_stopped.load(std::memory_order::acquire))
            {
                return unexpected<queue_consume_result>(queue_consume_result::queue_stopped);
            }

            if constexpr (std::is_move_constructible_v<element_type>)
            {
                return std::move(m_element.value());
            }
            else
            {
                return m_element.value();
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

    queue(const queue&) = delete;
    queue(queue&& other)
    {
        m_waiters  = std::exchange(other.m_waiters, nullptr);
        m_mutex    = std::move(other.m_mutex);
        m_elements = std::move(other.m_elements);
    }

    auto operator=(const queue&) -> queue& = delete;
    auto operator=(queue&& other) -> queue&
    {
        if (std::addressof(other) != this)
        {
            m_waiters  = std::exchange(other.m_waiters, nullptr);
            m_mutex    = std::move(other.m_mutex);
            m_elements = std::move(other.m_elements);
        }

        return *this;
    }

    /// @brief Determines if the queue is empty.
    /// @return True if the queue has no elements.
    auto empty() const -> bool { return size() == 0; }

    /// @brief Gets the current number of elements in the queue.
    /// @return The number of elements in the queue.
    auto size() const -> std::size_t
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_elements.size();
    }

    /// @brief Pushes the element into the queue.
    /// @param element The element to push.
    /// @return void.
    auto push(const element_type& element) -> queue_produce_result
    {
        if (m_stopped.load(std::memory_order::acquire))
        {
            return queue_produce_result::queue_stopped;
        }

        // The general idea is to see if anyone is waiting, and if so directly transfer the element
        // to that waiter. If there is nobody waiting then move the element into the queue.
        std::unique_lock lock{m_mutex};

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

        return queue_produce_result::produced;
    }

    /// @brief Pushes the element into the queue.
    /// @param element The element to push.
    /// @return void.
    auto push(element_type&& element) -> queue_produce_result
    {
        if (m_stopped.load(std::memory_order::acquire))
        {
            return queue_produce_result::queue_stopped;
        }

        std::unique_lock lock{m_mutex};

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

        return queue_produce_result::produced;
    }

    /// @brief Emplaces the element into the queue.
    /// @tparam ...args_type The element's constructor argument types.
    /// @param ...args The element's constructor arguments.
    /// @return void.
    template<class... args_type>
    auto emplace(args_type&&... args) -> queue_produce_result
    {
        if (m_stopped.load(std::memory_order::acquire))
        {
            return queue_produce_result::queue_stopped;
        }

        std::unique_lock lock{m_mutex};

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

        return queue_produce_result::produced;
    }

    /// @brief Pops the head element of the queue if available, or waits for one to
    ///        be available.
    /// @return The head element, or coro::queue_consume_result::queue_stopped if the
    ///         queue has been shutdown.
    [[nodiscard]] auto pop() -> awaiter { return awaiter{*this}; }

    /// @brief Shutsdown the queue and notifies all waiters of pop() to stop waiting.
    /// @return void.
    auto shutdown_notify_waiters() -> void
    {
        auto expected = false;
        if (!m_stopped.compare_exchange_strong(expected, true, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            return;
        }

        std::unique_lock lock{m_mutex};
        while (m_waiters != nullptr)
        {
            auto* to_resume = m_waiters;
            m_waiters       = m_waiters->m_next;

            lock.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lock.lock();
        }
    }

private:
    friend awaiter;
    /// @brief The list of pop() awaiters.
    awaiter* m_waiters{nullptr};
    /// @brief Mutex for properly maintaining the queue.
    std::mutex m_mutex{};
    /// @brief The underlying queue datastructure.
    std::queue<element_type> m_elements{};
    /// @brief Has this queue been shutdown?
    std::atomic<bool> m_stopped{false};
};

} // namespace coro

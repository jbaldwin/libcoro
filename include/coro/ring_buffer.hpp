#pragma once

#include "coro/concepts/executor.hpp"
#include "coro/detail/awaiter_list.hpp"
#include "coro/expected.hpp"
#include "coro/mutex.hpp"
#include "coro/sync_wait.hpp"
#include "coro/task.hpp"

#include <array>
#include <atomic>
#include <coroutine>
#include <memory>
#include <optional>

namespace coro
{
namespace ring_buffer_result
{
enum class produce
{
    produced,
    notified,
    stopped
};

enum class consume
{
    notified,
    stopped
};
} // namespace ring_buffer_result

/**
 * @tparam element The type of element the ring buffer will store.  Note that this type should be
 *         cheap to move if possible as it is moved into and out of the buffer upon produce and
 *         consume operations.
 * @tparam num_elements The maximum number of elements the ring buffer can store, must be >= 1.
 */
template<typename element, size_t num_elements>
class ring_buffer
{
private:
    enum running_state_t
    {
        /// @brief The ring buffer is still running.
        running,
        /// @brief The ring buffer is draining all elements, produce is no longer allowed.
        draining,
        /// @brief The ring buffer is fully shutdown, all produce and consume tasks will be woken up with result::stopped.
        stopped,
    };

public:
    /**
     * static_assert If `num_elements` == 0.
     */
    ring_buffer()
    {
        static_assert(num_elements != 0, "num_elements cannot be zero");
    }

    ~ring_buffer()
    {
        // Wake up anyone still using the ring buffer.
        coro::sync_wait(shutdown());
    }

    ring_buffer(const ring_buffer<element, num_elements>&) = delete;
    ring_buffer(ring_buffer<element, num_elements>&&)      = delete;

    auto operator=(const ring_buffer<element, num_elements>&) noexcept -> ring_buffer<element, num_elements>& = delete;
    auto operator=(ring_buffer<element, num_elements>&&) noexcept -> ring_buffer<element, num_elements>&      = delete;

    struct produce_operation
    {
        produce_operation(ring_buffer<element, num_elements>& rb, element e)
            : m_rb(rb),
              m_e(std::move(e))
        {}

        auto await_ready() noexcept -> bool
        {
            auto& mutex = m_rb.m_mutex;

            // Produce operations can only proceed if running.
            if (m_rb.m_running_state.load(std::memory_order::acquire) != running_state_t::running)
            {
                m_result = ring_buffer_result::produce::stopped;
                mutex.unlock();
                return true; // Will be awoken with produce::stopped
            }

            if (m_rb.m_used.load(std::memory_order::acquire) < num_elements)
            {
                // There is guaranteed space to store
                auto slot = m_rb.m_front.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                m_rb.m_elements[slot] = std::move(m_e);
                m_rb.m_used.fetch_add(1, std::memory_order::release);
                mutex.unlock();
                return true; // Will be awoken with produce::produced
            }

            return false; // ring buffer full, suspend
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            m_awaiting_coroutine = awaiting_coroutine;
            m_next = m_rb.m_produce_waiters.exchange(this, std::memory_order::acq_rel);
            m_rb.m_mutex.unlock();
            return true;
        }

        /**
         * @return produce_result
         */
        auto await_resume() -> ring_buffer_result::produce
        {
            return m_result;
        }

        /// If the operation needs to suspend, the coroutine to resume when the element can be produced.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// The result that should be returned when this coroutine resumes.
        ring_buffer_result::produce m_result{ring_buffer_result::produce::produced};
        /// Linked list of produce operations that are awaiting to produce their element.
        produce_operation* m_next{nullptr};

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class ring_buffer;

        /// The ring buffer the element is being produced into.
        ring_buffer<element, num_elements>& m_rb;
        /// The element this produce operation is producing into the ring buffer.
        std::optional<element> m_e{std::nullopt};
    };

    struct consume_operation
    {
        explicit consume_operation(ring_buffer<element, num_elements>& rb)
            : m_rb(rb)
        {}

        auto await_ready() noexcept -> bool
        {
            auto& mutex = m_rb.m_mutex;

            // Consume operations proceed until stopped.
            if (m_rb.m_running_state.load(std::memory_order::acquire) == running_state_t::stopped)
            {
                m_result = ring_buffer_result::consume::stopped;
                mutex.unlock();
                return true;
            }

            if (m_rb.m_used.load(std::memory_order::acquire) > 0)
            {
                auto slot = m_rb.m_back.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                m_e = std::move(m_rb.m_elements[slot]);
                m_rb.m_elements[slot] = std::nullopt;
                m_rb.m_used.fetch_sub(1, std::memory_order::release);
                mutex.unlock();
                return true;
            }

            return false; // ring buffer is empty, suspend.
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            m_awaiting_coroutine = awaiting_coroutine;
            m_next = m_rb.m_consume_waiters.exchange(this, std::memory_order::acq_rel);
            m_rb.m_mutex.unlock();
            return true;
        }

        /**
         * @return The consumed element or ring_buffer_stopped if the ring buffer has been shutdown.
         */
        auto await_resume() -> expected<element, ring_buffer_result::consume>
        {
            if (m_e.has_value())
            {
                return expected<element, ring_buffer_result::consume>(std::move(m_e).value());
            }
            else // state is stopped
            {
                return unexpected<ring_buffer_result::consume>(m_result);
            }
        }

        /// If the operation needs to suspend, the coroutine to resume when the element can be consumed.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// The unexpected result this should return on resume
        ring_buffer_result::consume m_result{ring_buffer_result::consume::stopped};
        /// Linked list of consume operations that are awaiting to consume an element.
        consume_operation* m_next{nullptr};

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class ring_buffer;

        /// The ring buffer to consume an element from.
        ring_buffer<element, num_elements>& m_rb;
        /// The element this consume operation will consume.
        std::optional<element> m_e{std::nullopt};
    };

    /**
     * Produces the given element into the ring buffer.  This operation will suspend until a slot
     * in the ring buffer becomes available.
     * @param e The element to produce.
     */
    [[nodiscard]] auto produce(element e) -> coro::task<ring_buffer_result::produce>
    {
        co_await m_mutex.lock();
        auto result = co_await produce_operation{*this, std::move(e)};
        co_await try_resume_consumers();
        co_return result;
    }

    /**
     * Consumes an element from the ring buffer.  This operation will suspend until an element in
     * the ring buffer becomes available.
     */
    [[nodiscard]] auto consume() -> coro::task<expected<element, ring_buffer_result::consume>>
    {
        co_await m_mutex.lock();
        auto result = co_await consume_operation{*this};
        co_await try_resume_producers();
        co_return result;
    }

    /**
     * @return The maximum number of elements the ring buffer can hold.
     */
    constexpr auto max_size() const noexcept -> size_t
    {
        return num_elements;
    }

    /**
     * @return The current number of elements contained in the ring buffer.
     */
    auto size() const -> size_t
    {
        return m_used.load(std::memory_order::acquire);
    }

    /**
     * @return True if the ring buffer contains zero elements.
     */
    [[nodiscard]] auto empty() const -> bool { return size() == 0; }

    /**
     * @return True if the ring buffer has no more space.
     */
    auto full() const -> bool { return size() == max_size(); }

    /**
     * @brief Wakes up all currently awaiting producers.  Their await_resume() function
     *        will return an expected produce result that producers have been notified.
     */
    auto notify_producers() -> coro::task<void>
    {
        auto expected = m_running_state.load(std::memory_order::acquire);
        if (expected == running_state_t::stopped)
        {
            co_return;
        }

        co_await m_mutex.lock();
        auto* produce_waiters = m_produce_waiters.exchange(nullptr, std::memory_order::acq_rel);
        m_mutex.unlock();

        while (produce_waiters != nullptr)
        {
            auto* next = produce_waiters->m_next;
            produce_waiters->m_result = ring_buffer_result::produce::notified;
            produce_waiters->m_awaiting_coroutine.resume();
            produce_waiters = next;
        }

        co_return;
    }

    /**
     * @brief Wakes up all currently awaiting consumers.  Their await_resume() function
     *        will return an expected consume result that consumers have been notified.
     */
    auto notify_consumers() -> coro::task<void>
    {
        auto expected = m_running_state.load(std::memory_order::acquire);
        if (expected == running_state_t::stopped)
        {
            co_return;
        }

        co_await m_mutex.lock();
        auto* consume_waiters = m_consume_waiters.exchange(nullptr, std::memory_order::acq_rel);
        m_mutex.unlock();

        while (consume_waiters != nullptr)
        {
            auto* next = consume_waiters->m_next;
            consume_waiters->m_result = ring_buffer_result::consume::notified;
            consume_waiters->m_awaiting_coroutine.resume();
            consume_waiters = next;
        }

        co_return;
    }

    /**
     * @brief Wakes up all currently awaiting producers and consumers.  Their await_resume() function
     *        will return an expected consume result that the ring buffer has stopped.
     */
    auto shutdown() -> coro::task<void>
    {
        // Only wake up waiters once.
        auto expected = m_running_state.load(std::memory_order::acquire);
        if (expected == running_state_t::stopped)
        {
            co_return;
        }

        auto lk = co_await m_mutex.scoped_lock();
        // Only let one caller do the wake-ups, this can go from running or draining to stopped
        if (!m_running_state.compare_exchange_strong(expected, running_state_t::stopped, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }
        lk.unlock();

        co_await m_mutex.lock();
        auto* produce_waiters = m_produce_waiters.exchange(nullptr, std::memory_order::acq_rel);
        auto* consume_waiters = m_consume_waiters.exchange(nullptr, std::memory_order::acq_rel);
        m_mutex.unlock();

        while (produce_waiters != nullptr)
        {
            auto* next = produce_waiters->m_next;
            produce_waiters->m_result = ring_buffer_result::produce::stopped;
            produce_waiters->m_awaiting_coroutine.resume();
            produce_waiters = next;
        }

        while (consume_waiters != nullptr)
        {
            auto* next = consume_waiters->m_next;
            consume_waiters->m_result = ring_buffer_result::consume::stopped;
            consume_waiters->m_awaiting_coroutine.resume();
            consume_waiters = next;
        }

        co_return;
    }

    template<coro::concepts::executor executor_type>
    [[nodiscard]] auto shutdown_drain(std::unique_ptr<executor_type>& e) -> coro::task<void>
    {
        auto lk = co_await m_mutex.scoped_lock();
        // Do not allow any more produces, the state must be in running to drain.
        auto expected = running_state_t::running;
        if (!m_running_state.compare_exchange_strong(expected, running_state_t::draining, std::memory_order::acq_rel, std::memory_order::relaxed))
        {
            co_return;
        }

        auto* produce_waiters = m_produce_waiters.exchange(nullptr, std::memory_order::acq_rel);
        lk.unlock();

        while (produce_waiters != nullptr)
        {
            auto* next = produce_waiters->m_next;
            produce_waiters->m_awaiting_coroutine.resume();
            produce_waiters = next;
        }

        while (!empty() && m_running_state.load(std::memory_order::acquire) == running_state_t::draining)
        {
            co_await e->yield();
        }

        co_await shutdown();
        co_return;
    }

    /**
     * Returns true if shutdown() or shutdown_drain() have been called on this coro::ring_buffer.
     * @return True if the coro::ring_buffer has been shutdown.
     */
    [[nodiscard]] auto is_shutdown() const -> bool { return m_running_state.load(std::memory_order::acquire) != running_state_t::running; }

private:
    friend produce_operation;
    friend consume_operation;

    coro::mutex m_mutex{};

    std::array<std::optional<element>, num_elements> m_elements{};
    /// The current front pointer to an open slot if not full.
    std::atomic<size_t> m_front{0};
    /// The current back pointer to the oldest item in the buffer if not empty.
    std::atomic<size_t> m_back{0};
    /// The number of items in the ring buffer.
    std::atomic<size_t> m_used{0};

    /// The LIFO list of produce waiters.
    std::atomic<produce_operation*> m_produce_waiters{nullptr};
    /// The LIFO list of consume watier.
    std::atomic<consume_operation*> m_consume_waiters{nullptr};

    std::atomic<running_state_t> m_running_state{running_state_t::running};

    auto try_resume_producers() -> coro::task<void>
    {
        while (true)
        {
            auto lk = co_await m_mutex.scoped_lock();
            if (m_used.load(std::memory_order::acquire) < num_elements)
            {
                auto* op = detail::awaiter_list_pop(m_produce_waiters);
                if (op != nullptr)
                {
                    auto slot = m_front.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                    m_elements[slot] = std::move(op->m_e);
                    m_used.fetch_add(1, std::memory_order::release);

                    lk.unlock();
                    op->m_awaiting_coroutine.resume();
                    continue;
                }
            }
            co_return;
        }
    }

    auto try_resume_consumers() -> coro::task<void>
    {
        while (true)
        {
            auto lk = co_await m_mutex.scoped_lock();
            if (m_used.load(std::memory_order::acquire) > 0)
            {
                auto* op = detail::awaiter_list_pop(m_consume_waiters);
                if (op != nullptr)
                {
                    auto slot = m_back.fetch_add(1, std::memory_order::acq_rel) % num_elements;
                    op->m_e = std::move(m_elements[slot]);
                    m_elements[slot] = std::nullopt;
                    m_used.fetch_sub(1, std::memory_order::release);
                    lk.unlock();

                    op->m_awaiting_coroutine.resume();
                    continue;
                }
            }
            co_return;
        }
    }
};

} // namespace coro

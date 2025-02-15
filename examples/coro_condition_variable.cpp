#include <coro/coro.hpp>
#include <iostream>
#include <queue>
#include <syncstream>

using namespace std::chrono_literals;

// Thread-safe queue
template<typename T>
class TSQueue
{
private:
    // Underlying queue
    std::queue<T> m_queue;

    // mutex for thread synchronization
    coro::mutex m_mutex;

    // Condition variable for signaling
    coro::condition_variable m_cond;

public:
    void set_scheduler(std::shared_ptr<coro::io_scheduler> scheduler) { m_cond.set_scheduler(scheduler); }

    // Pushes an element to the queue
    auto push(T item) -> coro::task<void>
    {
        // Acquire lock
        auto lock = co_await m_mutex.lock();

        // Add item
        m_queue.push(item);

        // Notify one thread that
        // is waiting
        m_cond.notify_one();
    }

    // Pops an element off the queue
    template<class Rep, class Period>
    auto pop(const std::chrono::duration<Rep, Period>& timeout) -> coro::task<coro::expected<T, coro::timeout_status>>
    {
        // acquire lock
        auto lock = co_await m_mutex.lock();

        // wait until queue is not empty
        if (!co_await m_cond.wait_for(lock, timeout, [this]() { return !m_queue.empty(); }))
            co_return coro::unexpected<coro::timeout_status>{coro::timeout_status::timeout};

        assert(!m_queue.empty());

        // retrieve item
        T item = m_queue.front();
        m_queue.pop();

        co_return coro::expected<T, coro::timeout_status>{item};
    }
};

struct Params
{
    int             max_value{};
    std::atomic_int next_value{};
    TSQueue<int>    queue{};
};

int main()
{
    auto scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{.pool = {.thread_count = 6}});
    std::vector<coro::task<void>> tasks{};
    auto                          params = std::make_shared<Params>();
    params->max_value                    = 10000;
    params->queue.set_scheduler(scheduler);

    // These tasks will wait until the given event has been set before advancing.
    auto make_producer_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                                 std::shared_ptr<Params>             p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        auto v = p->next_value.fetch_add(1);
        while (v < p->max_value)
        {
            co_await p->queue.push(v);
            v = p->next_value.fetch_add(1);

            co_await scheduler->yield();
        }

        co_return;
    };

    // This task will trigger the event allowing all waiting tasks to proceed.
    auto make_consumer_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                                 std::shared_ptr<Params>             p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        while (true)
        {
            auto v = co_await p->queue.pop(1s);

            if (!v.has_value())
                co_return;

            std::osyncstream(std::cout) << v.value() << std::endl;

            co_await scheduler->yield();
        }

        co_return;
    };

    for (int i = 0; i < 3; ++i)
    {
        tasks.emplace_back(make_consumer_task(scheduler, params));
    }

    for (int i = 0; i < 3; ++i)
    {
        tasks.emplace_back(make_producer_task(scheduler, params));
    }

    // Wait for all tasks to complete.
    coro::sync_wait(coro::when_all(std::move(tasks)));
    std::osyncstream(std::cout) << "finished" << std::endl;
    return 0;
}

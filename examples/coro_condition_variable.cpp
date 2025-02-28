#include <coro/coro.hpp>
#include <iostream>
#include <queue>
#include <vector>

using namespace std::chrono_literals;

// Coroutine thread-safe queue
template<typename T>
class TSQueue
{
private:
    // Underlying queue
    std::queue<T> m_queue;

    // mutex for coroutine synchronization
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

        // Notify one coroutine that
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
        {
            co_return coro::unexpected<coro::timeout_status>{coro::timeout_status::timeout};
        }

        assert(!m_queue.empty());

        // retrieve item
        T item = m_queue.front();
        m_queue.pop();

        co_return coro::expected<T, coro::timeout_status>{item};
    }
};

struct Params
{
    int              max_value{};
    std::atomic_int  next_value{};
    TSQueue<int>     queue{};
    std::vector<int> output;
};

int main()
{
    auto scheduler = coro::io_scheduler::make_shared(coro::io_scheduler::options{.pool = {.thread_count = 6}});
    std::vector<coro::task<void>> tasks{};
    auto                          params = std::make_shared<Params>();
    params->max_value                    = 20;
    params->queue.set_scheduler(scheduler);

    // These tasks will produce values ​​and put them into a queue
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

    // These tasks will consume values ​​from the queue, and wait for new data if the queue is empty
    auto make_consumer_task = [](std::shared_ptr<coro::io_scheduler> scheduler,
                                 std::shared_ptr<Params>             p) -> coro::task<void>
    {
        // Immediately schedule onto the scheduler.
        co_await scheduler->schedule();

        while (true)
        {
            auto v = co_await p->queue.pop(1s);

            if (!v.has_value())
            {
                co_return;
            }

            p->output.push_back(v.value());

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
    for (auto& v : params->output)
    {
        std::cout << v << std::endl;
    }

    std::cout << "finished" << std::endl;
    return 0;
}

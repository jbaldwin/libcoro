#include "catch_amalgamated.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <iostream>
#include <thread>

TEST_CASE("ring_buffer", "[ring_buffer]")
{
    std::cerr << "[ring_buffer]\n\n";
}

TEST_CASE("ring_buffer single element", "[ring_buffer]")
{
    const size_t                   iterations = 10;
    coro::ring_buffer<uint64_t, 1> rb{};

    std::vector<uint64_t> output{};

    auto make_producer_task = [](coro::ring_buffer<uint64_t, 1>& rb, size_t iterations) -> coro::task<void>
    {
        for (size_t i = 1; i <= iterations; ++i)
        {
            std::cerr << "produce: " << i << "\n";
            co_await rb.produce(i);
        }
        co_return;
    };

    auto make_consumer_task =
        [](coro::ring_buffer<uint64_t, 1>& rb, size_t iterations, std::vector<uint64_t>& output) -> coro::task<void>
    {
        for (size_t i = 1; i <= iterations; ++i)
        {
            auto expected = co_await rb.consume();
            auto value    = std::move(*expected);

            std::cerr << "consume: " << value << "\n";
            output.emplace_back(std::move(value));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(make_producer_task(rb, iterations), make_consumer_task(rb, iterations, output)));

    for (size_t i = 1; i <= iterations; ++i)
    {
        REQUIRE(output[i - 1] == i);
    }

    REQUIRE(rb.empty());
}

TEST_CASE("ring_buffer many elements many producers many consumers", "[ring_buffer]")
{
    {
    std::cerr << "BEGIN ring_buffer many elements many producers many consumers\n";
    const size_t iterations = 1'000'000;
    const size_t consumers  = 100;
    const size_t producers  = 100;

    coro::ring_buffer<uint64_t, 64> rb{};
    auto tp = coro::thread_pool::make_shared(coro::thread_pool::options{.thread_count = 4});
    coro::latch                     wait{producers};
    std::atomic<uint64_t> counter{0};
    std::atomic<uint64_t> initated_consumes{0};
    std::atomic<uint64_t> successful_consumes{0};
    std::atomic<uint64_t> producer_counter{0};

    auto make_producer_task =
        [](std::shared_ptr<coro::thread_pool> tp, coro::ring_buffer<uint64_t, 64>& rb, coro::latch& w, std::atomic<uint64_t>& producer_counter) -> coro::task<void>
    {
        co_await tp->schedule();
        auto to_produce = iterations / producers;

        for (size_t i = 1; i <= to_produce; ++i)
        {
            if (co_await rb.produce(i) != coro::ring_buffer_result::produce::produced)
            {
                std::cerr << "rb.produce(" << i << ") == coro::ring_buffer_result::produce::stopped\n";
            }
            else
            {
                producer_counter.fetch_add(i, std::memory_order::seq_cst);
            }
        }

        w.count_down();
        co_return;
    };

    auto make_shutdown_task =
        [](std::shared_ptr<coro::thread_pool> tp, coro::ring_buffer<uint64_t, 64>& rb, coro::latch& w) -> coro::task<void>
    {
        // Wait for all producers to complete before signally shutdown with drain.
        co_await tp->schedule();
        co_await w;

        while (!rb.empty())
        {
            co_await tp->yield();
        }

        co_await rb.shutdown_drain(tp);
        co_return;
    };

    auto make_consumer_task = [](std::shared_ptr<coro::thread_pool> tp, coro::ring_buffer<uint64_t, 64>& rb, std::atomic<uint64_t>& counter, std::atomic<uint64_t>& successful_consumes, std::atomic<uint64_t>& initated_consumes) -> coro::task<void>
    {
        co_await tp->schedule();

        // For the sanity of this test to complete consistently we'll consume the exact number of times
        // the reuslting REQUIREs at the end don't always line up perfectly with this many threads and the shutdown.
        auto consumes = iterations / producers;
        for(uint64_t i = 0; i < consumes; ++i)
        // while (true)
        {
            initated_consumes++;
            auto expected = co_await rb.consume();
            if (!expected)
            {
                break;
            }

            successful_consumes++;
            auto item = std::move(*expected);
            counter.fetch_add(item, std::memory_order::seq_cst);

            co_await tp->yield(); // mimic some work
        }

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(consumers * producers + 1);

    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task(tp, rb, counter, successful_consumes, initated_consumes));
    }
    for (size_t i = 0; i < producers; ++i)
    {
        tasks.emplace_back(make_producer_task(tp, rb, wait, producer_counter));
    }
    tasks.emplace_back(make_shutdown_task(tp, rb, wait));

    coro::sync_wait(coro::when_all(std::move(tasks)));
    std::cerr << "initated_consumes=[" << initated_consumes << "]\n";
    std::cerr << "successful_consumes=[" << successful_consumes << "]\n";

    REQUIRE(rb.empty());
    REQUIRE(successful_consumes == iterations);
    REQUIRE(producer_counter == 5000500000);
    REQUIRE(counter == 5000500000);
    }
    std::cerr << "END ring_buffer many elements many producers many consumers\n";
}

TEST_CASE("ring_buffer producer consumer separate threads", "[ring_buffer]")
{
    // This test explicitly tests two independent threads producing and consuming from a ring_buffer.
    // Issue #120 reported a race condition when the ring buffer is accessed on separate threads.

    const size_t                   iterations = 10'000'000;
    coro::ring_buffer<uint64_t, 2> rb{};

    // We'll use an io schedule so we can use yield_for on shutdown since its two threads.
    auto producer_tp = coro::thread_pool::make_shared(coro::thread_pool::options{.thread_count = 1});
    auto consumer_tp = coro::thread_pool::make_shared(coro::thread_pool::options{.thread_count = 1});

    auto make_producer_task = [](std::shared_ptr<coro::thread_pool> producer_tp, coro::ring_buffer<uint64_t, 2>& rb) -> coro::task<void>
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            // This test has to constantly reschedule onto the other thread since the produce
            // and consume calls will end up switching which "thread" is processing.
            co_await producer_tp->schedule();
            co_await rb.produce(i);
        }

        co_await rb.shutdown_drain(producer_tp);
        co_return;
    };

    auto make_consumer_task = [](std::shared_ptr<coro::thread_pool> consumer_tp, coro::ring_buffer<uint64_t, 2>& rb) -> coro::task<void>
    {
        while (true)
        {
            co_await consumer_tp->schedule();
            auto expected = co_await rb.consume();
            if (!expected)
            {
                break;
            }

            auto item = std::move(*expected);
            (void)item;

            co_await consumer_tp->yield(); // mimic some work
        }

        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.emplace_back(make_producer_task(producer_tp, rb));
    tasks.emplace_back(make_consumer_task(consumer_tp, rb));

    coro::sync_wait(coro::when_all(std::move(tasks)));

    REQUIRE(rb.empty());
}

TEST_CASE("ring_buffer issue-242 default constructed complex objects on consume", "[ring_buffer]")
{
    std::cerr << "BEGIN ring_buffer issue-242 default constructed complex objects on consume\n";

    struct message
    {
        message(uint32_t i, std::string t) : id(i), text(std::move(t)) {}
        message(const message&) = delete;
        message(message&& other) : id(other.id), text(std::move(other.text)) {}
        auto operator=(const message&) -> message& = delete;
        auto operator=(message&& other) -> message&
        {
            if (std::addressof(other) != this)
            {
                this->id   = std::exchange(other.id, 0);
                this->text = std::move(other.text);
            }

            return *this;
        }

        ~message() { id = 0; }

        uint32_t    id;
        std::string text;
    };

    struct example
    {
        example() { std::cerr << "I'm being created\n"; }
        example(const example&) = delete;
        example(example&& other) : msg(std::move(other.msg))
        {
            std::cerr << "i'm being moved constructed with msg = ";
            if (msg.has_value())
            {
                std::cerr << "id = " << msg.value().id << ", msg = " << msg.value().text << "\n";
            }
            else
            {
                std::cerr << "nullopt\n";
            }
        }

        ~example()
        {
            std::cerr << "I'm being deleted with msg = ";
            if (msg.has_value())
            {
                std::cerr << "id = " << msg.value().id << ", msg = " << msg.value().text << "\n";
            }
            else
            {
                std::cerr << "nullopt\n";
            }
        }

        auto operator=(const example&) -> example& = delete;
        auto operator=(example&& other) -> example&
        {
            if (std::addressof(other) != this)
            {
                this->msg = std::move(other.msg);

                std::cerr << "i'm being moved assigned with msg = ";
                if (msg.has_value())
                {
                    std::cerr << msg.value().id << ", " << msg.value().text << "\n";
                }
                else
                {
                    std::cerr << "nullopt\n";
                }
            }

            return *this;
        }

        std::optional<message> msg{std::nullopt};
    };

    coro::ring_buffer<example, 1> buffer;

    const auto produce = [](coro::ring_buffer<example, 1>& buffer) -> coro::task<void>
    {
        std::cerr << "enter produce coroutine\n";
        example data{};
        data.msg = {message{1, "Hello World!"}};
        std::cerr << "ID: " << data.msg.value().id << "\n";
        std::cerr << "Text: " << data.msg.value().text << "\n";
        std::cerr << "buffer.produce(move(data)) start\n";
        auto result = co_await buffer.produce(std::move(data));
        std::cerr << "buffer.produce(move(data)) done\n";
        REQUIRE(result == coro::ring_buffer_result::produce::produced);
        std::cerr << "exit produce coroutine\n";
        co_return;
    };

    coro::sync_wait(produce(buffer));
    std::cerr << "enter sync_wait\n";
    auto result = coro::sync_wait(buffer.consume());
    std::cerr << "exit sync_wait\n";
    REQUIRE(result);

    auto& data = result.value();
    REQUIRE(data.msg.has_value());
    REQUIRE(data.msg.value().id == 1);
    REQUIRE(data.msg.value().text == "Hello World!");
    std::cerr << "Outside the coroutine\n";
    std::cerr << "ID: " << data.msg.value().id << "\n";
    std::cerr << "Text: " << data.msg.value().text << "\n";
}

TEST_CASE("ring_buffer issue-242 default constructed complex objects on consume in coroutines", "[ring_buffer]")
{
    std::cerr << "BEGIN ring_buffer issue-242 default constructed complex objects on consume in coroutines\n";

    struct message
    {
        uint32_t    id;
        std::string text;
    };

    struct example
    {
        example() {}
        example(const example&) = delete;
        example(example&& other) : msg(std::move(other.msg)) {}

        auto operator=(const example&) -> example& = delete;
        auto operator=(example&& other) -> example&
        {
            if (std::addressof(other) != this)
            {
                this->msg = std::move(other.msg);
            }

            return *this;
        }

        std::optional<message> msg{std::nullopt};
    };

    coro::ring_buffer<example, 1> buffer;

    const auto produce = [](coro::ring_buffer<example, 1>& buffer) -> coro::task<void>
    {
        example data{};
        data.msg = {message{.id = 1, .text = "Hello World!"}};
        std::cerr << "Inside the coroutine\n";
        std::cerr << "ID: " << data.msg.value().id << "\n";
        std::cerr << "Text: " << data.msg.value().text << "\n";
        auto result = co_await buffer.produce(std::move(data));
        REQUIRE(result == coro::ring_buffer_result::produce::produced);
        co_return;
    };

    const auto consume = [](coro::ring_buffer<example, 1>& buffer) -> coro::task<example>
    {
        auto result = co_await buffer.consume();
        REQUIRE(result.has_value());
        REQUIRE(result.value().msg.has_value());
        auto data = std::move(*result);
        co_return std::move(data);
    };

    std::cerr << "coro::sync_wait(produce(buffer))\n";
    coro::sync_wait(produce(buffer));
    std::cerr << "coro::sync_wait(consume(buffer))\n";
    auto data = coro::sync_wait(consume(buffer));

    REQUIRE(data.msg.has_value());
    REQUIRE(data.msg.value().id == 1);
    REQUIRE(data.msg.value().text == "Hello World!");
    std::cerr << "Outside the coroutine\n";
    std::cerr << "ID: " << data.msg.value().id << "\n";
    std::cerr << "Text: " << data.msg.value().text << "\n";
    std::cerr << "END ring_buffer issue-242 complex\n";
}

TEST_CASE("ring_buffer issue-242 basic type", "[ring_buffer]")
{
    std::cerr << "BEGIN ring_buffer issue-242 basic type\n";
    coro::ring_buffer<uint32_t, 1> buffer;

    const auto foo = [](coro::ring_buffer<uint32_t, 1>& buffer) -> coro::task<void>
    {
        co_await buffer.produce(1);
        co_return;
    };

    coro::sync_wait(foo(buffer));
    auto result = coro::sync_wait(buffer.consume());
    REQUIRE(result);

    auto data = std::move(*result);
    REQUIRE(data == 1);
    std::cerr << "END ring_buffer issue-242 basic type\n";
}

TEST_CASE("~ring_buffer", "[ring_buffer]")
{
    std::cerr << "[~ring_buffer]\n\n";
}

#include "catch.hpp"

#include <coro/coro.hpp>

#include <thread>
#include <chrono>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::chrono_literals;

TEST_CASE("engine submit single functor")
{
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        std::cerr << "Hello world from engine task!\n";
        counter++;
        co_return;
    }();

    e.execute(task);

    // while(counter != 1) std::this_thread::sleep_for(1ms);

    e.shutdown();

    REQUIRE(counter == 1);
}

TEST_CASE("engine submit mutiple tasks")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(n);

    auto func = [&]() -> coro::task<void> { counter++; co_return; };
    for(std::size_t i = 0; i < n; ++i)
    {
        tasks.emplace_back(func());
        e.execute(tasks.back());
    }
    e.shutdown();

    REQUIRE(counter == n);
}

TEST_CASE("engine task with multiple yields on event")
{
    std::atomic<uint64_t> counter{0};
    coro::engine e{};
    coro::engine_event<uint64_t> event1{e};
    coro::engine_event<uint64_t> event2{e};
    coro::engine_event<uint64_t> event3{e};

    auto task = [&]() -> coro::task<void>
    {
        std::cerr << "1st suspend\n";
        co_await e.yield(event1);
        std::cerr << "1st resume\n";
        counter += event1.result();
        std::cerr << "never suspend\n";
        co_await std::suspend_never{};
        std::cerr << "2nd suspend\n";
        co_await e.yield(event2);
        std::cerr << "2nd resume\n";
        counter += event2.result();
        std::cerr << "3rd suspend\n";
        co_await e.yield(event3);
        std::cerr << "3rd resume\n";
        counter += event3.result();
        co_return;
    }();

    auto resume_task = [&](coro::engine_event<uint64_t>& event, int expected) {
        event.set(1);
        while(counter != expected)
        {
            std::cerr << "counter=" << counter << "\n";
            std::this_thread::sleep_for(1ms);
        }
    };

    e.execute(task);

    resume_task(event1, 1);
    resume_task(event2, 2);
    resume_task(event3, 3);

    e.shutdown();

    REQUIRE(e.empty());
}

TEST_CASE("engine task with read poll")
{
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        // Poll will block until there is data to read.
        co_await e.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(true);
        co_return;
    }();

    e.execute(task);

    uint64_t value{42};
    write(trigger_fd, &value, sizeof(value));

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine task with read")
{
    constexpr uint64_t expected_value{42};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        uint64_t val{0};
        auto bytes_read = co_await e.read(
            trigger_fd,
            std::span<char>(reinterpret_cast<char*>(&val), sizeof(val))
        );

        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    }();

    e.execute(task);

    write(trigger_fd, &expected_value, sizeof(expected_value));

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine task with read and write same fd")
{
    // Since this test uses an eventfd, only 1 task at a time can poll/read/write to that
    // event descriptor through the engine.  It could be possible to modify the engine
    // to keep track of read and write events on a specific socket/fd and update the tasks
    // as well as resumes accordingly, right now this is just a known limitation, see the
    // pipe test for two concurrent tasks read and write awaiting on different file descriptors.

    constexpr uint64_t expected_value{9001};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        auto bytes_written = co_await e.write(
            trigger_fd,
            std::span<const char>(reinterpret_cast<const char*>(&expected_value), sizeof(expected_value))
        );

        REQUIRE(bytes_written == sizeof(uint64_t));

        uint64_t val{0};
        auto bytes_read = co_await e.read(
            trigger_fd,
            std::span<char>(reinterpret_cast<char*>(&val), sizeof(val))
        );

        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    }();

    e.execute(task);

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine task with read and write pipe")
{
    const std::string msg{"coroutines are really cool but not that EASY!"};
    int pipe_fd[2];
    pipe2(pipe_fd, O_NONBLOCK);

    coro::engine e{};

    auto read_task = [&]() -> coro::task<void>
    {
        std::string buffer(4096, '0');
        std::span<char> view{buffer.data(), buffer.size()};
        auto bytes_read = co_await e.read(pipe_fd[0], view);
        REQUIRE(bytes_read == msg.size());
        buffer.resize(bytes_read);
        REQUIRE(buffer == msg);
    }();

    auto write_task = [&]() -> coro::task<void>
    {
        std::span<const char> view{msg.data(), msg.size()};
        auto bytes_written = co_await e.write(pipe_fd[1], view);
        REQUIRE(bytes_written == msg.size());
    }();

    e.execute(read_task);
    e.execute(write_task);

    e.shutdown();
    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

static auto standalone_read(
    coro::engine& e,
    coro::engine::fd_type socket,
    std::span<char> buffer
) -> coro::task<ssize_t>
{
    // do other stuff in larger function
    co_return co_await e.read(socket, buffer);
    // do more stuff in larger function
}

TEST_CASE("engine standalone read task")
{
    constexpr ssize_t expected_value{1111};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        ssize_t v{0};
        auto bytes_read = co_await standalone_read(e, trigger_fd, std::span<char>(reinterpret_cast<char*>(&v), sizeof(v)));
        REQUIRE(bytes_read == sizeof(ssize_t));

        REQUIRE(v == expected_value);
        co_return;
    }();

    e.execute(task);

    write(trigger_fd, &expected_value, sizeof(expected_value));

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine separate thread resume")
{
    coro::engine e{};

    // This lambda will mimic a 3rd party service which will execute on a service on a background thread.
    // Uses the passed event handle to resume execution of the awaiting corountine on the engine.
    auto third_party_service = [](coro::engine_event<void>& handle) -> coro::task<void>
    {
        // Normally this thread is probably already running for real world use cases.
        std::thread third_party_thread([](coro::engine_event<void>& h) -> void {
            // mimic some expensive computation
            // std::this_thread::sleep_for(1s);
            h.set();
        }, std::ref(handle));

        third_party_thread.detach();

        co_await handle;
        co_return;
    };

    auto task = [&]() -> coro::task<void>
    {
        coro::engine_event<void> handle{e};
        co_await third_party_service(handle);
        REQUIRE(true);
    }();

    e.execute(task);
    e.shutdown();
}

TEST_CASE("engine separate thread resume with return")
{
    constexpr uint64_t expected_value{1337};
    coro::engine e{};

    std::atomic<coro::engine_event<uint64_t>*> event{};

    std::thread service{
        [&]() -> void
        {
            while(event == nullptr)
            {
                std::this_thread::sleep_for(1ms);
            }

            event.load()->set(expected_value);
        }
    };

    auto third_party_service = [&](int multiplier) -> coro::task<uint64_t>
    {
        auto output = co_await e.yield<uint64_t>([&](coro::engine_event<uint64_t>& ev) {
            event = &ev;
        });
        co_return output * multiplier;
    };

    auto task = [&]() -> coro::task<void>
    {
        int multiplier{5};
        uint64_t value = co_await third_party_service(multiplier);
        REQUIRE(value == (expected_value * multiplier));
    }();

    e.execute(task);

    service.join();
    e.shutdown();
}

TEST_CASE("engine with normal task")
{
    constexpr std::size_t expected_value{5};
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    auto add_data = [&](uint64_t val) -> coro::task<int>
    {
        co_return val;
    };

    auto task1 = [&]() -> coro::task<void>
    {
        counter += co_await add_data(expected_value);
        co_return;
    }();

    e.execute(task1);
    e.shutdown();

    REQUIRE(counter == expected_value);
}

TEST_CASE("engine trigger growth of internal tasks storage")
{
    std::atomic<uint64_t> counter{0};
    constexpr std::size_t iterations{512};
    coro::engine e{1};

    auto wait_func = [&](uint64_t id, std::chrono::milliseconds wait_time) -> coro::task<void>
    {
        co_await e.yield_for(wait_time);
        ++counter;
        co_return;
    };

    std::vector<coro::task<void>> tasks{};
    tasks.reserve(iterations);
    for(std::size_t i = 0; i < iterations; ++i)
    {
        tasks.emplace_back(wait_func(i, std::chrono::milliseconds{50}));
        e.execute(tasks.back());
    }

    e.shutdown();

    REQUIRE(counter == iterations);
}

TEST_CASE("engine yield with engine event void")
{
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        co_await e.yield<void>(
            [&](coro::engine_event<void>& event) -> void
            {
                event.set();
            }
        );

        counter += 42;
        co_return;
    }();

    e.execute(task);

    e.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("engine yield with engine event uint64_t")
{
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    auto task = [&]() -> coro::task<void>
    {
        counter += co_await e.yield<uint64_t>(
            [&](coro::engine_event<uint64_t>& event) -> void
            {
                event.set(42);
            }
        );

        co_return;
    }();

    e.execute(task);

    e.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("engine yield user provided event")
{
    std::string expected_result = "Here I am!";
    coro::engine e{};
    coro::engine_event<std::string> event{e};

    auto task = [&]() -> coro::task<void>
    {
        co_await e.yield(event);
        REQUIRE(event.result() == expected_result);
        co_return;
    }();

    e.execute(task);

    event.set(expected_result);

    e.shutdown();
}
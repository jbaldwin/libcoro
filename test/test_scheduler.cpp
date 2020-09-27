#include "catch.hpp"

#include <coro/coro.hpp>

#include <thread>
#include <chrono>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::chrono_literals;

TEST_CASE("scheduler submit single functor")
{
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        std::cerr << "Hello world from scheduler task!\n";
        counter++;
        co_return;
    }();

    s.schedule(std::move(task));

    s.shutdown();

    REQUIRE(counter == 1);
}

TEST_CASE("scheduler submit mutiple tasks")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};

    auto func = [&]() -> coro::task<void> { counter++; co_return; };
    for(std::size_t i = 0; i < n; ++i)
    {
        s.schedule(func());
    }
    s.shutdown();

    REQUIRE(counter == n);
}

TEST_CASE("scheduler task with multiple yields on event")
{
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};
    coro::resume_token<uint64_t> token{s};

    auto task = [&]() -> coro::task<void>
    {
        std::cerr << "1st suspend\n";
        co_await s.yield(token);
        std::cerr << "1st resume\n";
        counter += token.result();
        token.reset();
        std::cerr << "never suspend\n";
        co_await std::suspend_never{};
        std::cerr << "2nd suspend\n";
        co_await s.yield(token);
        token.reset();
        std::cerr << "2nd resume\n";
        counter += token.result();
        std::cerr << "3rd suspend\n";
        co_await s.yield(token);
        token.reset();
        std::cerr << "3rd resume\n";
        counter += token.result();
        co_return;
    }();

    auto resume_task = [&](coro::resume_token<uint64_t>& token, int expected) {
        token.resume(1);
        while(counter != expected)
        {
            std::cerr << "counter=" << counter << "\n";
            std::this_thread::sleep_for(1ms);
        }
    };

    s.schedule(std::move(task));

    resume_task(token, 1);
    resume_task(token, 2);
    resume_task(token, 3);

    s.shutdown();

    REQUIRE(s.empty());
}

TEST_CASE("scheduler task with read poll")
{
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        // Poll will block until there is data to read.
        co_await s.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(true);
        co_return;
    }();

    s.schedule(std::move(task));

    uint64_t value{42};
    write(trigger_fd, &value, sizeof(value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("scheduler task with read")
{
    constexpr uint64_t expected_value{42};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        uint64_t val{0};
        auto bytes_read = co_await s.read(
            trigger_fd,
            std::span<char>(reinterpret_cast<char*>(&val), sizeof(val))
        );

        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    }();

    s.schedule(std::move(task));

    write(trigger_fd, &expected_value, sizeof(expected_value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("scheduler task with read and write same fd")
{
    // Since this test uses an eventfd, only 1 task at a time can poll/read/write to that
    // event descriptor through the scheduler.  It could be possible to modify the scheduler
    // to keep track of read and write events on a specific socket/fd and update the tasks
    // as well as resumes accordingly, right now this is just a known limitation, see the
    // pipe test for two concurrent tasks read and write awaiting on different file descriptors.

    constexpr uint64_t expected_value{9001};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        auto bytes_written = co_await s.write(
            trigger_fd,
            std::span<const char>(reinterpret_cast<const char*>(&expected_value), sizeof(expected_value))
        );

        REQUIRE(bytes_written == sizeof(uint64_t));

        uint64_t val{0};
        auto bytes_read = co_await s.read(
            trigger_fd,
            std::span<char>(reinterpret_cast<char*>(&val), sizeof(val))
        );

        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    }();

    s.schedule(std::move(task));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("scheduler task with read and write pipe")
{
    const std::string msg{"coroutines are really cool but not that EASY!"};
    int pipe_fd[2];
    pipe2(pipe_fd, O_NONBLOCK);

    coro::scheduler s{};

    auto read_task = [&]() -> coro::task<void>
    {
        std::string buffer(4096, '0');
        std::span<char> view{buffer.data(), buffer.size()};
        auto bytes_read = co_await s.read(pipe_fd[0], view);
        REQUIRE(bytes_read == msg.size());
        buffer.resize(bytes_read);
        REQUIRE(buffer == msg);
    }();

    auto write_task = [&]() -> coro::task<void>
    {
        std::span<const char> view{msg.data(), msg.size()};
        auto bytes_written = co_await s.write(pipe_fd[1], view);
        REQUIRE(bytes_written == msg.size());
    }();

    s.schedule(std::move(read_task));
    s.schedule(std::move(write_task));

    s.shutdown();
    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

static auto standalone_read(
    coro::scheduler& s,
    coro::scheduler::fd_type socket,
    std::span<char> buffer
) -> coro::task<ssize_t>
{
    // do other stuff in larger function
    co_return co_await s.read(socket, buffer);
    // do more stuff in larger function
}

TEST_CASE("scheduler standalone read task")
{
    constexpr ssize_t expected_value{1111};
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        ssize_t v{0};
        auto bytes_read = co_await standalone_read(s, trigger_fd, std::span<char>(reinterpret_cast<char*>(&v), sizeof(v)));
        REQUIRE(bytes_read == sizeof(ssize_t));

        REQUIRE(v == expected_value);
        co_return;
    }();

    s.schedule(std::move(task));

    write(trigger_fd, &expected_value, sizeof(expected_value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("scheduler separate thread resume")
{
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        // User manual resume token, create one specifically for each task being generated
        coro::resume_token<void> token{s};

        // Normally this thread is probably already running for real world use cases, but in general
        // the 3rd party function api will be set, they should have "user data" void* or ability
        // to capture variables via lambdas for on complete callbacks, here we mimic an on complete
        // callback by capturing the hande.
        std::thread third_party_thread([&token]() -> void {
            // mimic some expensive computation
            // std::this_thread::sleep_for(1s);
            token.resume();
        });
        third_party_thread.detach();

        // Wait on the handle until the 3rd party service is completed.
        co_await token;
        REQUIRE(true);
    }();

    s.schedule(std::move(task));
    s.shutdown();
}

TEST_CASE("scheduler separate thread resume with return")
{
    constexpr uint64_t expected_value{1337};
    coro::scheduler s{};

    std::atomic<coro::resume_token<uint64_t>*> token{};

    std::thread service{
        [&]() -> void
        {
            while(token == nullptr)
            {
                std::this_thread::sleep_for(1ms);
            }

            token.load()->resume(expected_value);
        }
    };

    auto third_party_service = [&](int multiplier) -> coro::task<uint64_t>
    {
        auto output = co_await s.yield<uint64_t>([&](coro::resume_token<uint64_t>& t) {
            token = &t;
        });
        co_return output * multiplier;
    };

    auto task = [&]() -> coro::task<void>
    {
        int multiplier{5};
        uint64_t value = co_await third_party_service(multiplier);
        REQUIRE(value == (expected_value * multiplier));
    }();

    s.schedule(std::move(task));

    service.join();
    s.shutdown();
}

TEST_CASE("scheduler with basic task")
{
    constexpr std::size_t expected_value{5};
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};

    auto add_data = [&](uint64_t val) -> coro::task<int>
    {
        co_return val;
    };

    auto task1 = [&]() -> coro::task<void>
    {
        counter += co_await add_data(expected_value);
        co_return;
    }();

    s.schedule(std::move(task1));
    s.shutdown();

    REQUIRE(counter == expected_value);
}

TEST_CASE("scheduler trigger growth of internal tasks storage")
{
    std::atomic<uint64_t> counter{0};
    constexpr std::size_t iterations{512};
    coro::scheduler s{1};

    auto wait_func = [&](uint64_t id, std::chrono::milliseconds wait_time) -> coro::task<void>
    {
        co_await s.yield_for(wait_time);
        ++counter;
        co_return;
    };

    for(std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(i, std::chrono::milliseconds{50}));
    }

    s.shutdown();

    REQUIRE(counter == iterations);
}

TEST_CASE("scheduler yield with scheduler event void")
{
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        co_await s.yield<void>(
            [&](coro::resume_token<void>& token) -> void
            {
                token.resume();
            }
        );

        counter += 42;
        co_return;
    }();

    s.schedule(std::move(task));

    s.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("scheduler yield with scheduler event uint64_t")
{
    std::atomic<uint64_t> counter{0};
    coro::scheduler s{};

    auto task = [&]() -> coro::task<void>
    {
        counter += co_await s.yield<uint64_t>(
            [&](coro::resume_token<uint64_t>& token) -> void
            {
                token.resume(42);
            }
        );

        co_return;
    }();

    s.schedule(std::move(task));

    s.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("scheduler yield user provided event")
{
    std::string expected_result = "Here I am!";
    coro::scheduler s{};
    coro::resume_token<std::string> token{s};

    auto task = [&]() -> coro::task<void>
    {
        co_await s.yield(token);
        REQUIRE(token.result() == expected_result);
        co_return;
    }();

    s.schedule(std::move(task));

    token.resume(expected_result);

    s.shutdown();
}
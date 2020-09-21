#include "catch.hpp"

#include <coro/coro.hpp>

#include <thread>
#include <chrono>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::chrono_literals;
using task_type = coro::engine::task_type;

TEST_CASE("engine submit single functor")
{
    std::atomic<uint64_t> counter{0};
    coro::engine e{};
    e.execute([&]() -> task_type { counter++; co_return; }());
    e.shutdown();

    REQUIRE(counter == 1);
}

TEST_CASE("engine submit mutiple tasks")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    coro::engine e{};

    auto func = [&]() -> task_type { counter++; co_return; };
    for(std::size_t i = 0; i < n; ++i)
    {
        e.execute(func());
    }
    e.shutdown();

    REQUIRE(counter == n);
}

TEST_CASE("engine task with multiple suspends and manual resumes")
{
    std::atomic<coro::engine_task_id_type> task_id{0};
    std::atomic<uint64_t> counter{0};

    coro::engine e{};
    auto resume_task = [&](int expected) {
        e.resume(task_id);
        while(counter != expected)
        {
            std::this_thread::sleep_for(1ms);
        }
    };

    auto task = [&]() -> task_type
    {
        co_await std::suspend_always{};
        ++counter;
        co_await std::suspend_never{};
        co_await std::suspend_always{};
        ++counter;
        co_await std::suspend_always{};
        ++counter;
        co_return;
    }();

    e.execute(std::move(task));

    resume_task(1);
    resume_task(2);
    resume_task(3);

    REQUIRE(e.empty());
}

TEST_CASE("engine task with read poll")
{
    auto trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::engine e{};

    auto task = [&]() -> task_type
    {
        // Poll will block until there is data to read.
        co_await e.poll(trigger_fd, coro::await_op::read);
        co_return;
    }();

    e.execute(std::move(task));

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

    auto task = [&]() -> task_type
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

    e.execute(std::move(task));

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

    auto task = [&]() -> task_type
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

    e.execute(std::move(task));

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine task with read and write pipe")
{
    const std::string msg{"coroutines are really cool but not that EASY!"};
    int pipe_fd[2];
    pipe2(pipe_fd, O_NONBLOCK);

    coro::engine e{};

    auto read_task = [&]() -> task_type
    {
        std::string buffer(4096, '0');
        std::span<char> view{buffer.data(), buffer.size()};
        auto bytes_read = co_await e.read(pipe_fd[0], view);
        REQUIRE(bytes_read == msg.size());
        buffer.resize(bytes_read);
        REQUIRE(buffer == msg);
    }();

    auto write_task = [&]() -> task_type
    {
        std::span<const char> view{msg.data(), msg.size()};
        auto bytes_written = co_await e.write(pipe_fd[1], view);
        REQUIRE(bytes_written == msg.size());
    }();

    e.execute(std::move(read_task));
    e.execute(std::move(write_task));

    e.shutdown();
    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

static auto standalone_read(
    coro::engine& e,
    coro::engine::socket_type socket,
    std::span<char> buffer
) -> coro::engine_task<ssize_t>
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

    auto task = [&]() -> task_type
    {
        ssize_t v{0};
        auto bytes_read = co_await standalone_read(e, trigger_fd, std::span<char>(reinterpret_cast<char*>(&v), sizeof(v)));
        REQUIRE(bytes_read == sizeof(ssize_t));

        REQUIRE(v == expected_value);
        co_return;
    }();

    e.execute(std::move(task));

    write(trigger_fd, &expected_value, sizeof(expected_value));

    e.shutdown();
    close(trigger_fd);
}

TEST_CASE("engine separate thread resume")
{
    coro::engine_task_id_type task_id;
    coro::engine e{};

    // This lambda will mimic a 3rd party service which will execute on a service on
    // a background thread;
    auto third_party_service = [&]() -> std::suspend_always
    {
        // Normally this thread is probably already running for real world use cases.
        std::thread third_party_thread([&]() -> void {
            // mimic some expensive computation
            // std::this_thread::sleep_for(1s);
            std::cerr << "task_id=" << task_id << "\n";
            e.resume(task_id);
        });
        third_party_thread.detach();
        return std::suspend_always{};
    };

    auto task = [&]() -> task_type
    {
        co_await third_party_service();
        REQUIRE(true);
    }();

    task_id = e.execute(std::move(task));
    e.shutdown();
}

TEST_CASE("engine separate thread resume with return")
{
    constexpr uint64_t expected_value{1337};
    coro::engine e{};

    struct shared_data
    {
        std::atomic<bool> ready{false};
        std::optional<coro::engine_task_id_type> task_id{};
        uint64_t output{0};
    } data{};

    std::thread service{
        [&]() -> void
        {
            while(!data.ready)
            {
                std::this_thread::sleep_for(1ms);
            }

            data.output = expected_value;
            e.resume(data.task_id.value());
        }
    };

    auto third_party_service = [&](int multiplier) -> coro::engine_task<uint64_t>
    {
        co_await e.suspend([&](auto task_id) { data.task_id = task_id; data.ready = true; });
        co_return data.output * multiplier;
    };

    auto task = [&]() -> task_type
    {
        int multiplier{5};
        uint64_t value = co_await third_party_service(multiplier);
        REQUIRE(value == (expected_value * multiplier));
    }();

    e.execute(std::move(task));

    e.shutdown();
    service.join();
}

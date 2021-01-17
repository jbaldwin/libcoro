#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

TEST_CASE("io_scheduler sizeof()")
{
    std::cerr << "sizeof(coro::io_scheduler)=[" << sizeof(coro::io_scheduler) << "]\n";
    std::cerr << "sizeof(coro:task<void>)=[" << sizeof(coro::task<void>) << "]\n";

    std::cerr << "sizeof(std::coroutine_handle<>)=[" << sizeof(std::coroutine_handle<>) << "]\n";
    std::cerr << "sizeof(std::variant<coro::task<void>, std::coroutine_handle<>>)=["
              << sizeof(std::variant<coro::task<void>, std::coroutine_handle<>>) << "]\n";

    REQUIRE(true);
}

TEST_CASE("io_scheduler submit single task")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    // Note that captures are only safe as long as the lambda object outlives the execution
    // of the coroutine.  In all of these tests the lambda is created on the root test function
    // and thus will always outlive the coroutines, but in a real application this is dangerous
    // and coroutine 'captures' should be passed in via paramters to the function to be copied
    // into the coroutines stack frame.  Lets
    auto func = [&]() -> coro::task<void> {
        std::cerr << "Hello world from scheduler task!\n";
        counter++;
        co_return;
    };

    auto task = func();
    s.schedule(std::move(task));

    s.shutdown();

    REQUIRE(counter == 1);
}

TEST_CASE("io_scheduler submit single task with move and auto initializing lambda")
{
    // This example test will auto invoke the lambda object, return the task and then destruct.
    // Because the lambda immediately goes out of scope the task must capture all variables
    // through its parameters directly.

    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto task = [](std::atomic<uint64_t>& counter) -> coro::task<void> {
        std::cerr << "Hello world from io_scheduler task!\n";
        counter++;
        co_return;
    }(counter);

    s.schedule(std::move(task));

    s.shutdown();

    REQUIRE(counter == 1);
}

TEST_CASE("io_scheduler submit mutiple tasks")
{
    constexpr std::size_t n = 1000;
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto func = [&]() -> coro::task<void> {
        counter++;
        co_return;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        s.schedule(func());
    }
    s.shutdown();

    REQUIRE(counter == n);
}

TEST_CASE("io_scheduler task with multiple yields on event")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};
    auto                  token = s.make_resume_token<uint64_t>();

    auto func = [&]() -> coro::task<void> {
        std::cerr << "1st suspend\n";
        co_await s.yield(token);
        std::cerr << "1st resume\n";
        counter += token.return_value();
        token.reset();
        std::cerr << "never suspend\n";
        co_await std::suspend_never{};
        std::cerr << "2nd suspend\n";
        co_await s.yield(token);
        token.reset();
        std::cerr << "2nd resume\n";
        counter += token.return_value();
        std::cerr << "3rd suspend\n";
        co_await s.yield(token);
        token.reset();
        std::cerr << "3rd resume\n";
        counter += token.return_value();
        co_return;
    };

    auto resume_task = [&](coro::resume_token<uint64_t>& token, uint64_t expected) {
        token.resume(1);
        while (counter != expected)
        {
            std::this_thread::sleep_for(1ms);
        }
    };

    s.schedule(func());

    resume_task(token, 1);
    resume_task(token, 2);
    resume_task(token, 3);

    s.shutdown();

    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler task with read poll")
{
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        // Poll will block until there is data to read.
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read);
        REQUIRE(status == coro::poll_status::event);
        co_return;
    };

    s.schedule(func());

    uint64_t value{42};
    write(trigger_fd, &value, sizeof(value));

    s.shutdown();
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read poll with timeout")
{
    using namespace std::chrono_literals;
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        // Poll with a timeout (but don't timeout).
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read, 50ms);
        REQUIRE(status == coro::poll_status::event);
        co_return;
    };

    s.schedule(func());

    uint64_t value{42};
    write(trigger_fd, &value, sizeof(value));

    s.shutdown();
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read poll timeout")
{
    using namespace std::chrono_literals;
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        // Poll with a timeout (but don't timeout).
        auto status = co_await s.poll(trigger_fd, coro::poll_op::read, 10ms);
        REQUIRE(status == coro::poll_status::timeout);
        co_return;
    };

    s.schedule(func());

    s.shutdown();
    REQUIRE(s.empty());
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read")
{
    constexpr uint64_t expected_value{42};
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        uint64_t val{0};
        auto [status, bytes_read] =
            co_await s.read(trigger_fd, std::span<char>(reinterpret_cast<char*>(&val), sizeof(val)));

        REQUIRE(status == coro::poll_status::event);
        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    };

    s.schedule(func());

    write(trigger_fd, &expected_value, sizeof(expected_value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read with timeout")
{
    using namespace std::chrono_literals;
    constexpr uint64_t expected_value{42};
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        uint64_t val{0};
        auto [status, bytes_read] =
            co_await s.read(trigger_fd, std::span<char>(reinterpret_cast<char*>(&val), sizeof(val)), 50ms);

        REQUIRE(status == coro::poll_status::event);
        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    };

    s.schedule(func());

    write(trigger_fd, &expected_value, sizeof(expected_value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read timeout")
{
    using namespace std::chrono_literals;
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        uint64_t val{0};
        auto [status, bytes_read] =
            co_await s.read(trigger_fd, std::span<char>(reinterpret_cast<char*>(&val), sizeof(val)), 10ms);

        REQUIRE(status == coro::poll_status::timeout);
        REQUIRE(bytes_read == 0);
        REQUIRE(val == 0);
        co_return;
    };

    s.schedule(func());

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read and write same fd")
{
    // Since this test uses an eventfd, only 1 task at a time can poll/read/write to that
    // event descriptor through the scheduler.  It could be possible to modify the scheduler
    // to keep track of read and write events on a specific socket/fd and update the tasks
    // as well as resumes accordingly, right now this is just a known limitation, see the
    // pipe test for two concurrent tasks read and write awaiting on different file descriptors.

    constexpr uint64_t expected_value{9001};
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        auto [read_status, bytes_written] = co_await s.write(
            trigger_fd, std::span<const char>(reinterpret_cast<const char*>(&expected_value), sizeof(expected_value)));

        REQUIRE(read_status == coro::poll_status::event);
        REQUIRE(bytes_written == sizeof(uint64_t));

        uint64_t val{0};
        auto [write_status, bytes_read] =
            co_await s.read(trigger_fd, std::span<char>(reinterpret_cast<char*>(&val), sizeof(val)));

        REQUIRE(write_status == coro::poll_status::event);
        REQUIRE(bytes_read == sizeof(uint64_t));
        REQUIRE(val == expected_value);
        co_return;
    };

    s.schedule(func());

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("io_scheduler task with read and write pipe")
{
    const std::string msg{"coroutines are really cool but not that EASY!"};
    int               pipe_fd[2];
    pipe2(pipe_fd, O_NONBLOCK);

    coro::io_scheduler s{};

    auto read_func = [&]() -> coro::task<void> {
        std::string     buffer(4096, '0');
        std::span<char> view{buffer.data(), buffer.size()};
        auto [status, bytes_read] = co_await s.read(pipe_fd[0], view);

        REQUIRE(status == coro::poll_status::event);
        REQUIRE(bytes_read == msg.size());
        buffer.resize(bytes_read);
        REQUIRE(buffer == msg);
    };

    auto write_func = [&]() -> coro::task<void> {
        std::span<const char> view{msg.data(), msg.size()};
        auto [status, bytes_written] = co_await s.write(pipe_fd[1], view);

        REQUIRE(status == coro::poll_status::event);
        REQUIRE(bytes_written == msg.size());
    };

    s.schedule(read_func());
    s.schedule(write_func());

    s.shutdown();
    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

static auto standalone_read(coro::io_scheduler& s, coro::io_scheduler::fd_t socket, std::span<char> buffer)
    -> coro::task<std::pair<coro::poll_status, ssize_t>>
{
    // do other stuff in larger function
    co_return co_await s.read(socket, buffer);
    // do more stuff in larger function
}

TEST_CASE("io_scheduler standalone read task")
{
    constexpr ssize_t  expected_value{1111};
    auto               trigger_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        ssize_t v{0};
        auto [status, bytes_read] =
            co_await standalone_read(s, trigger_fd, std::span<char>(reinterpret_cast<char*>(&v), sizeof(v)));

        REQUIRE(status == coro::poll_status::event);
        REQUIRE(bytes_read == sizeof(ssize_t));
        REQUIRE(v == expected_value);
        co_return;
    };

    s.schedule(func());

    write(trigger_fd, &expected_value, sizeof(expected_value));

    s.shutdown();
    close(trigger_fd);
}

TEST_CASE("io_scheduler separate thread resume")
{
    coro::io_scheduler s{};

    auto func = [&]() -> coro::task<void> {
        // User manual resume token, create one specifically for each task being generated
        // coro::resume_token<void> token{s};
        auto token = s.make_resume_token<void>();

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
    };

    s.schedule(func());
    s.shutdown();
}

TEST_CASE("io_scheduler separate thread resume with return")
{
    constexpr uint64_t expected_value{1337};
    coro::io_scheduler s{};

    std::atomic<coro::resume_token<uint64_t>*> token{};

    std::thread service{[&]() -> void {
        while (token == nullptr)
        {
            std::this_thread::sleep_for(1ms);
        }

        token.load()->resume(expected_value);
    }};

    auto third_party_service = [&](int multiplier) -> coro::task<uint64_t> {
        auto              output = co_await s.yield<uint64_t>([&](coro::resume_token<uint64_t>& t) { token = &t; });
        co_return output* multiplier;
    };

    auto func = [&]() -> coro::task<void> {
        int      multiplier{5};
        uint64_t value = co_await third_party_service(multiplier);
        REQUIRE(value == (expected_value * multiplier));
    };

    s.schedule(func());

    service.join();
    s.shutdown();
}

TEST_CASE("io_scheduler with basic task")
{
    constexpr std::size_t expected_value{5};
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto add_data = [&](uint64_t val) -> coro::task<int> { co_return val; };

    auto func = [&]() -> coro::task<void> {
        counter += co_await add_data(expected_value);
        co_return;
    };

    s.schedule(func());
    s.shutdown();

    REQUIRE(counter == expected_value);
}

TEST_CASE("io_scheduler scheduler_after")
{
    constexpr std::chrono::milliseconds wait_for{50};
    std::atomic<uint64_t>               counter{0};
    coro::io_scheduler                  s{};

    auto func = [&]() -> coro::task<void> {
        ++counter;
        co_return;
    };

    auto start = std::chrono::steady_clock::now();
    s.schedule_after(func(), wait_for);
    s.shutdown();
    auto stop     = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    REQUIRE(counter == 1);
    REQUIRE(duration >= wait_for);
}

TEST_CASE("io_scheduler schedule_at")
{
    // Because schedule_at() will take its own time internally the wait_for might be off by a bit.
    constexpr std::chrono::milliseconds epsilon{3};
    constexpr std::chrono::milliseconds wait_for{50};
    std::atomic<uint64_t>               counter{0};
    coro::io_scheduler                  s{};

    auto func = [&]() -> coro::task<void> {
        ++counter;
        co_return;
    };

    // now or in the past will be rejected.
    REQUIRE_FALSE(s.schedule_at(func(), std::chrono::steady_clock::now()));
    REQUIRE_FALSE(s.schedule_at(func(), std::chrono::steady_clock::now() - std::chrono::seconds{1}));

    auto start = std::chrono::steady_clock::now();
    s.schedule_at(func(), std::chrono::steady_clock::now() + wait_for);
    s.shutdown();
    auto stop     = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    REQUIRE(counter == 1);
    REQUIRE(duration >= (wait_for - epsilon));
}

TEST_CASE("io_scheduler trigger growth of internal tasks storage")
{
    std::atomic<uint64_t> counter{0};
    constexpr std::size_t iterations{512};
    coro::io_scheduler    s{coro::io_scheduler::options{.reserve_size = 1}};

    auto wait_func = [&](std::chrono::milliseconds wait_time) -> coro::task<void> {
        co_await s.yield_for(wait_time);
        ++counter;
        co_return;
    };

    for (std::size_t i = 0; i < iterations; ++i)
    {
        s.schedule(wait_func(std::chrono::milliseconds{50}));
    }

    s.shutdown();

    REQUIRE(counter == iterations);
}

TEST_CASE("io_scheduler yield with scheduler event void")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto func = [&]() -> coro::task<void> {
        co_await s.yield<void>([&](coro::resume_token<void>& token) -> void { token.resume(); });

        counter += 42;
        co_return;
    };

    s.schedule(func());

    s.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("io_scheduler yield with scheduler event uint64_t")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto func = [&]() -> coro::task<void> {
        counter += co_await s.yield<uint64_t>([&](coro::resume_token<uint64_t>& token) -> void { token.resume(42); });

        co_return;
    };

    s.schedule(func());

    s.shutdown();

    REQUIRE(counter == 42);
}

TEST_CASE("io_scheduler yield user event")
{
    std::string        expected_result = "Here I am!";
    coro::io_scheduler s{};
    auto               token = s.make_resume_token<std::string>();

    auto func = [&]() -> coro::task<void> {
        co_await s.yield(token);
        REQUIRE(token.return_value() == expected_result);
        co_return;
    };

    s.schedule(func());

    token.resume(expected_result);

    s.shutdown();
}

TEST_CASE("io_scheduler yield user event multiple waiters")
{
    std::atomic<int>   counter{0};
    coro::io_scheduler s{};
    auto               token = s.make_resume_token<void>();

    auto func = [&](int amount) -> coro::task<void> {
        co_await token;
        std::cerr << "amount=" << amount << "\n";
        counter += amount;
    };

    s.schedule(func(1));
    s.schedule(func(2));
    s.schedule(func(3));
    s.schedule(func(4));
    s.schedule(func(5));

    std::this_thread::sleep_for(20ms);

    token.resume();

    // This will bypass co_await since its already resumed.
    s.schedule(func(10));

    s.shutdown();

    REQUIRE(counter == 25);
}

TEST_CASE("io_scheduler manual process events with self generating coroutine (stack overflow check)")
{
    uint64_t           counter{0};
    coro::io_scheduler s{coro::io_scheduler::options{.thread_strategy = coro::io_scheduler::thread_strategy_t::manual}};

    auto func = [&](auto f) -> coro::task<void> {
        ++counter;

        // this should detect stack overflows well enough
        if (counter % 1'000'000 == 0)
        {
            co_return;
        }

        s.schedule(f(f));
        co_return;
    };

    std::cerr << "Scheduling recursive function.\n";
    s.schedule(func(func));

    while (s.process_events())
        ;
    std::cerr << "Recursive test done.\n";
}

TEST_CASE("io_scheduler task throws")
{
    coro::io_scheduler s{};

    auto func = []() -> coro::task<void> {
        // Is it possible to actually notify the user when running a task in a scheduler?
        // Seems like the user will need to manually catch within the task themselves.
        throw std::runtime_error{"I always throw."};
        co_return;
    };

    s.schedule(func());

    s.shutdown();
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler task throws after resume")
{
    coro::io_scheduler s{};
    auto               token = s.make_resume_token<void>();

    auto func = [&]() -> coro::task<void> {
        co_await token;
        throw std::runtime_error{"I always throw."};
        co_return;
    };

    s.schedule(func());

    std::this_thread::sleep_for(50ms);
    token.resume();

    s.shutdown();
    REQUIRE(s.empty());
}

TEST_CASE("io_scheduler schedule parameter pack tasks")
{
    coro::io_scheduler s{};

    std::atomic<uint64_t> counter{0};
    auto                  make_task = [&]() -> coro::task<void> {
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    s.schedule(make_task(), make_task(), make_task(), make_task(), make_task());

    s.shutdown();
    REQUIRE(s.empty());
    REQUIRE(counter == 5);
}

TEST_CASE("io_scheduler schedule vector<task>")
{
    coro::io_scheduler s{};

    std::atomic<uint64_t> counter{0};
    auto                  make_task = [&]() -> coro::task<void> {
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    std::vector<coro::task<void>> tasks;
    tasks.reserve(4);
    tasks.emplace_back(make_task());
    tasks.emplace_back(make_task());
    tasks.emplace_back(make_task());
    tasks.emplace_back(make_task());

    s.schedule(std::move(tasks));

    REQUIRE(tasks.empty());

    s.shutdown();
    REQUIRE(s.empty());
    REQUIRE(counter == 4);
}

TEST_CASE("io_scheduler yield()")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    // This task will check the counter and yield if it isn't 5.
    auto make_wait_task = [&]() -> coro::task<void> {
        while (counter.load(std::memory_order::relaxed) < 5)
        {
            std::cerr << "count = " << counter.load(std::memory_order::relaxed) << "\n";
            co_await s.yield();
        }
        co_return;
    };

    // This task will increment counter by 1 and yield after each increment.
    auto make_inc_task = [&]() -> coro::task<void> {
        while (counter.load(std::memory_order::relaxed) < 5)
        {
            std::cerr << "increment!\n";
            counter.fetch_add(1, std::memory_order::relaxed);
            co_await s.yield();
        }
        co_return;
    };

    s.schedule(make_wait_task());
    s.schedule(make_inc_task());
    s.shutdown();

    REQUIRE(counter == 5);
}

TEST_CASE("io_scheduler multiple timed waits")
{
    std::atomic<uint64_t> counter{0};
    coro::io_scheduler    s{};

    auto start_point = std::chrono::steady_clock::now();

    auto make_task = [&]() -> coro::task<void> {
        auto now   = std::chrono::steady_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_point);
        std::cerr << "task counter = " << counter.load(std::memory_order::relaxed) << " elapsed = " << epoch.count()
                  << "\n";
        counter.fetch_add(1, std::memory_order::relaxed);
        co_return;
    };

    auto start = std::chrono::steady_clock::now();
    s.schedule_after(make_task(), std::chrono::milliseconds{5});
    s.schedule_after(make_task(), std::chrono::milliseconds{5});
    s.schedule_after(make_task(), std::chrono::milliseconds{20});
    s.schedule_after(make_task(), std::chrono::milliseconds{50});
    s.schedule_after(make_task(), std::chrono::milliseconds{50});
    s.shutdown();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    REQUIRE(counter == 5);
    REQUIRE(elapsed.count() >= 50);
}

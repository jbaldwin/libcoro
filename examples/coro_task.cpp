#include <coro/coro.hpp>
#include <iostream>

int main()
{
    // Create a task that awaits the doubling of its given value and
    // then returns the result after adding 5.
    auto double_and_add_5_task = [](uint64_t input) -> coro::task<uint64_t>
    {
        // Task that takes a value and doubles it.
        auto double_task = [](uint64_t x) -> coro::task<uint64_t> { co_return x * 2; };

        auto doubled = co_await double_task(input);
        co_return doubled + 5;
    };

    auto output = coro::sync_wait(double_and_add_5_task(2));
    std::cout << "Task1 output = " << output << "\n";

    struct expensive_struct
    {
        std::string              id{};
        std::vector<std::string> records{};

        expensive_struct()  = default;
        ~expensive_struct() = default;

        // Explicitly delete copy constructor and copy assign, force only moves!
        // While the default move constructors will work for this struct the example
        // inserts explicit print statements to show the task is moving the value
        // out correctly.
        expensive_struct(const expensive_struct&)                    = delete;
        auto operator=(const expensive_struct&) -> expensive_struct& = delete;

        expensive_struct(expensive_struct&& other) : id(std::move(other.id)), records(std::move(other.records))
        {
            std::cout << "expensive_struct() move constructor called\n";
        }
        auto operator=(expensive_struct&& other) -> expensive_struct&
        {
            if (std::addressof(other) != this)
            {
                id      = std::move(other.id);
                records = std::move(other.records);
            }
            std::cout << "expensive_struct() move assignment called\n";
            return *this;
        }
    };

    // Create a very large object and return it by moving the value so the
    // contents do not have to be copied out.
    auto move_output_task = []() -> coro::task<expensive_struct>
    {
        expensive_struct data{};
        data.id = "12345678-1234-5678-9012-123456781234";
        for (size_t i = 10'000; i < 100'000; ++i)
        {
            data.records.emplace_back(std::to_string(i));
        }

        // Because the struct only has move contructors it will be forced to use
        // them, no need to explicitly std::move(data).
        co_return data;
    };

    auto data = coro::sync_wait(move_output_task());
    std::cout << data.id << " has " << data.records.size() << " records.\n";

    // std::unique_ptr<T> can also be used to return a larger object.
    auto unique_ptr_task = []() -> coro::task<std::unique_ptr<uint64_t>> { co_return std::make_unique<uint64_t>(42); };

    auto answer_to_everything = coro::sync_wait(unique_ptr_task());
    if (answer_to_everything != nullptr)
    {
        std::cout << "Answer to everything = " << *answer_to_everything << "\n";
    }
}

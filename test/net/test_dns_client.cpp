#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>

TEST_CASE("dns_client basic")
{
    coro::io_scheduler scheduler{
        coro::io_scheduler::options{.thread_strategy = coro::io_scheduler::thread_strategy_t::spawn}
    };

    coro::net::dns_client dns_client{scheduler, std::chrono::milliseconds{5000}};

    std::atomic<bool> done{false};

    auto make_host_by_name_task = [&](coro::net::hostname hn) -> coro::task<void>
    {
        auto result_ptr = co_await std::move(dns_client.host_by_name(hn));

        if(result_ptr->status() == coro::net::dns_status::complete)
        {
            for(const auto& ip_addr : result_ptr->ip_addresses())
            {
                std::cerr << coro::net::to_string(ip_addr.domain()) << " " << ip_addr.to_string() << "\n";
            }
        }

        done = true;

        co_return;
    };

    scheduler.schedule(make_host_by_name_task(coro::net::hostname{"www.example.com"}));

    while(!done)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    scheduler.shutdown();
    REQUIRE(scheduler.empty());
}
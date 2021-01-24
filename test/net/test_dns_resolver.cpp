#include "catch.hpp"

#include <coro/coro.hpp>

#include <chrono>

TEST_CASE("dns_resolver basic")
{
    coro::io_scheduler scheduler{coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}}};
    coro::net::dns_resolver dns_resolver{scheduler, std::chrono::milliseconds{5000}};

    auto make_host_by_name_task = [&](coro::net::hostname hn) -> coro::task<void> {
        co_await scheduler.schedule();
        auto result_ptr = co_await std::move(dns_resolver.host_by_name(hn));

        if (result_ptr->status() == coro::net::dns_status::complete)
        {
            for (const auto& ip_addr : result_ptr->ip_addresses())
            {
                std::cerr << coro::net::to_string(ip_addr.domain()) << " " << ip_addr.to_string() << "\n";
            }
        }

        co_return;
    };

    coro::sync_wait(make_host_by_name_task(coro::net::hostname{"www.example.com"}));

    scheduler.shutdown();
    REQUIRE(scheduler.empty());
}
#include "catch_amalgamated.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include <coro/coro.hpp>

    #include <chrono>

TEST_CASE("dns_resolver basic", "[dns]")
{
    auto scheduler = coro::io_scheduler::make_shared(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});
    coro::net::dns::resolver<coro::io_scheduler> dns_resolver{scheduler, std::chrono::milliseconds{5000}};

    auto make_host_by_name_task = [](std::shared_ptr<coro::io_scheduler>           scheduler,
                                     coro::net::dns::resolver<coro::io_scheduler>& dns_resolver,
                                     coro::net::hostname                           hn) -> coro::task<void>
    {
        co_await scheduler->schedule();
        auto result_ptr = co_await std::move(dns_resolver.host_by_name(hn));

        if (result_ptr->status() == coro::net::dns::status::complete)
        {
            for (const auto& ip_addr : result_ptr->ip_addresses())
            {
                std::cerr << coro::net::to_string(ip_addr.domain()) << " " << ip_addr.to_string() << "\n";
            }
        }

        co_return;
    };

    coro::sync_wait(make_host_by_name_task(scheduler, dns_resolver, coro::net::hostname{"www.example.com"}));

    std::cerr << "io_scheduler.size() before shutdown = " << scheduler->size() << "\n";
    scheduler->shutdown();
    std::cerr << "io_scheduler.size() after shutdown = " << scheduler->size() << "\n";
    REQUIRE(scheduler->empty());
}

#endif // LIBCORO_FEATURE_NETWORKING

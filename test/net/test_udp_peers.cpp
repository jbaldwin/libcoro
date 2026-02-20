#include "catch_amalgamated.hpp"
#include "coro/net/socket_address.hpp"

#ifdef LIBCORO_FEATURE_NETWORKING

    #include "catch_net_extensions.hpp"
    #include <coro/coro.hpp>

TEST_CASE("udp one way", "[udp]")
{
    const std::string msg{"aaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbcccccccccccccccccc"};
    const auto        endpoint = coro::net::socket_address{"127.0.0.1", 8080};

    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_send_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                             const std::string&                msg,
                             const coro::net::socket_address&  endpoint) -> coro::task<void>
    {
        co_await scheduler->schedule();
        coro::net::udp::peer peer{scheduler};

        auto wstatus = co_await peer.write_to(endpoint, msg);
        REQUIRE_THAT(wstatus, IsOk());

        co_return;
    };

    auto make_recv_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                             const std::string&                msg,
                             const coro::net::socket_address&  endpoint) -> coro::task<void>
    {
        co_await scheduler->schedule();
        const auto listen_point = coro::net::socket_address{"0.0.0.0", endpoint.port()};

        coro::net::udp::peer self{scheduler, listen_point};

        std::string buffer(64, '\0');
        auto [rstatus, peer_endpoint, rspan] = co_await self.read_from(buffer);
        REQUIRE_THAT(rstatus, IsOk());
        REQUIRE(peer_endpoint.ip() == endpoint.ip());
        // The peer's port will be randomly picked by the kernel since it wasn't bound.
        REQUIRE(rspan.size() == msg.size());
        buffer.resize(rspan.size());
        REQUIRE(buffer == msg);

        co_return;
    };

    coro::sync_wait(coro::when_all(make_recv_task(scheduler, msg, endpoint), make_send_task(scheduler, msg, endpoint)));
}

TEST_CASE("udp echo peers", "[udp]")
{
    const std::string peer1_msg{"Hello from peer1!"};
    const std::string peer2_msg{"Hello from peer2!!"};

    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_peer_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                             uint16_t                          my_port,
                             uint16_t                          peer_port,
                             bool                              send_first,
                             const std::string                 my_msg,
                             const std::string                 peer_msg) -> coro::task<void>
    {
        co_await scheduler->schedule();
        const auto my_endpoint   = coro::net::socket_address{"0.0.0.0", my_port};
        const auto peer_endpoint = coro::net::socket_address{"127.0.0.1", peer_port};

        coro::net::udp::peer me{scheduler, my_endpoint};

        if (send_first)
        {
            // Send my message to my peer first.
            auto wstatus = co_await me.write_to(peer_endpoint, my_msg);
            REQUIRE_THAT(wstatus, IsOk());
        }
        else
        {
            std::string buffer(64, '\0');
            auto [rstatus, recv_peer_endpoint, rspan] = co_await me.read_from(buffer);
            REQUIRE_THAT(rstatus, IsOk());
            REQUIRE(recv_peer_endpoint == peer_endpoint);
            REQUIRE(rspan.size() == peer_msg.size());
            buffer.resize(rspan.size());
            REQUIRE(buffer == peer_msg);
        }

        if (send_first)
        {
            // I sent first so now I need to await my peer's message.
            std::string buffer(64, '\0');
            auto [rstatus, recv_peer_endpoint, rspan] = co_await me.read_from(buffer);
            REQUIRE_THAT(rstatus, IsOk());
            REQUIRE(recv_peer_endpoint == peer_endpoint);
            REQUIRE(rspan.size() == peer_msg.size());
            buffer.resize(rspan.size());
            REQUIRE(buffer == peer_msg);
        }
        else
        {
            auto sstatus = co_await me.write_to(peer_endpoint, my_msg);
            REQUIRE_THAT(sstatus, IsOk());
        }

        co_return;
    };

    coro::sync_wait(
        coro::when_all(
            make_peer_task(scheduler, 8081, 8080, false, peer2_msg, peer1_msg),
            make_peer_task(scheduler, 8080, 8081, true, peer1_msg, peer2_msg)));
}

TEST_CASE("udp nullptr scheduler", "[udp]")
{
    std::unique_ptr<coro::scheduler> scheduler;
    const auto                       address = coro::net::socket_address{"127.0.0.1", 8080};

    CHECK_THROWS_AS((coro::net::udp::peer{scheduler}), std::runtime_error);
    CHECK_THROWS_AS((coro::net::udp::peer{scheduler, address}), std::runtime_error);
}

TEST_CASE("udp basic checks", "[udp]")
{
    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});
    auto peer = coro::net::udp::peer{scheduler, {"127.0.0.1", 9090}};

    auto make_peer_task = [](const std::unique_ptr<coro::scheduler>& scheduler,
                             coro::net::udp::peer&                   peer) -> coro::task<void>
    {
        co_await scheduler->schedule();
        std::array<std::byte, 0> buf{};
        auto                     status = co_await peer.write_to({"127.0.0.1", 8080}, buf);
        CHECK_THAT(status, IsOk());
    };

    coro::sync_wait(make_peer_task(scheduler, peer));
}

TEST_CASE("udp timeout", "[udp]")
{
    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto clientAddress = GENERATE(
        coro::net::socket_address{"127.0.0.1", 8081},
        coro::net::socket_address{"::1", 8081, coro::net::domain_t::ipv6});
    auto        timeout = std::chrono::milliseconds{25};
    coro::event clientFinishEvent;

    auto make_client_task = [](std::unique_ptr<coro::scheduler>& scheduler,
                               const coro::net::socket_address&  clientAddress,
                               coro::event&                      clientFinishEvent,
                               const std::chrono::milliseconds&  timeout) -> coro::task<void>
    {
        auto                      peer = coro::net::udp::peer{scheduler, clientAddress};
        std::array<std::byte, 16> buf{};

        auto IsTimeout = IsError(coro::net::io_status::kind::timeout);

        // No reason to test write, it's very hard to get timeout because of the UDP nature.

        auto [rstatus, address, read] = co_await peer.read_from(buf, timeout);
        CHECK_THAT(rstatus, IsTimeout);
        clientFinishEvent.set();
    };

    DYNAMIC_SECTION("Domain: " << to_string(clientAddress.domain()))
    {
        coro::sync_wait(make_client_task(scheduler, clientAddress, clientFinishEvent, timeout));
    }
}

TEST_CASE("udp not bound", "[udp]")
{
    auto scheduler =
        coro::scheduler::make_unique(coro::scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto task = [](std::unique_ptr<coro::scheduler>& scheduler) -> coro::task<void>
    {
        co_await scheduler->schedule();

        auto peer = coro::net::udp::peer{scheduler};

        std::string buf{};
        auto [status, address, read] = co_await peer.read_from(buf);
        REQUIRE_THAT(status, IsError(coro::net::io_status::kind::udp_not_bound));
        CHECK(read.empty());
    };

    coro::sync_wait(task(scheduler));
}

#endif // LIBCORO_FEATURE_NETWORKING

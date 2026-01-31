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

#endif // LIBCORO_FEATURE_NETWORKING

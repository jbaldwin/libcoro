#include <coro/coro.hpp>
#include <iostream>

int main()
{
    auto scheduler = std::make_shared<coro::io_scheduler>(
        coro::io_scheduler::options{.pool = coro::thread_pool::options{.thread_count = 1}});

    auto make_server_task = [&]() -> coro::task<void> {
        // This is the task that will handle processing a client's requests.
        auto serve_client = [](coro::net::tcp_client client) -> coro::task<void> {
            size_t requests{1};

            while (true)
            {
                // Continue to accept more requests until the client closes the connection.
                co_await client.poll(coro::poll_op::read);

                std::string request(64, '\0');
                auto [recv_status, recv_bytes] = client.recv(request);
                if (recv_status == coro::net::recv_status::closed)
                {
                    break;
                }

                request.resize(recv_bytes.size());
                std::cout << "server: " << request << "\n";

                // Make sure the client socket can be written to.
                co_await client.poll(coro::poll_op::write);

                auto response = "Hello from server " + std::to_string(requests);
                client.send(response);

                ++requests;
            }

            co_return;
        };

        // Spin up the tcp_server and schedule it onto the io_scheduler.
        coro::net::tcp_server server{scheduler};
        co_await scheduler->schedule();

        // All incoming connections will be stored into the task container until they are completed.
        coro::task_container tc{scheduler};

        // Wait for an incoming connection and accept it, this example will only use 1 connection.
        co_await server.poll();
        auto client = server.accept();
        // Store the task that will serve the client into the container and immediately begin executing it
        // on the task container's thread pool, which is the same as the scheduler.
        tc.start(serve_client(std::move(client)));

        // Wait for all clients to complete before shutting down the tcp_server.
        co_await tc.garbage_collect_and_yield_until_empty();
        co_return;
    };

    auto make_client_task = [&](size_t request_count) -> coro::task<void> {
        co_await scheduler->schedule();
        coro::net::tcp_client client{scheduler};

        co_await client.connect();

        // Send N requests on the same connection and wait for the server response to each one.
        for (size_t i = 1; i <= request_count; ++i)
        {
            // Make sure the client socket can be written to.
            co_await client.poll(coro::poll_op::write);

            // Send the request data.
            auto request = "Hello from client " + std::to_string(i);
            client.send(request);

            co_await client.poll(coro::poll_op::read);
            std::string response(64, '\0');
            auto [recv_status, recv_bytes] = client.recv(response);
            response.resize(recv_bytes.size());

            std::cout << "client: " << response << "\n";
        }

        co_return; // Upon exiting the tcp_client will close its connection to the server.
    };

    coro::sync_wait(coro::when_all(make_server_task(), make_client_task(5)));
}

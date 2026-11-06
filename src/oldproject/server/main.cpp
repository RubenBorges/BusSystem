#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <web/client.hpp>
#include <web/server.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>

// Entry point that starts a demo server and a matching client in the same process.
int main() {
    namespace asio = boost::asio;

//SERVER SIDE
    asio::io_context server_io(1);
    auto server = p2p::web::make_server(
        server_io, p2p::web::ServerOptions{4544, "/shutdown"});
    server->start();

    std::thread server_thread([&server_io]() {
        server_io.run();
    });

    asio::io_context client_io(1);
    auto client_work = asio::make_work_guard(client_io);
    std::thread client_thread([&client_io]() {
        client_io.run();
    });
    
//CLIENT SIDE
    p2p::web::AsyncEchoClient client(client_io);

    auto connect_future = asio::co_spawn(
        client_io,
        client.connect(p2p::web::ClientOptions{"127.0.0.1", 4544}),
        asio::use_future);
    connect_future.get();

    std::cout << "Connected. Type lines to echo. Send /shutdown to stop the server.\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        auto reply_future = asio::co_spawn(
            client_io,
            client.send_and_receive(line),
            asio::use_future);

        std::cout << "Server: " << reply_future.get() << "\n";

        if (line == "/shutdown") {
            break;
        }
    }

    client.close();
    client_work.reset();
    client_io.stop();
    server->request_stop();

    client_thread.join();
    server_thread.join();
    return 0;
}

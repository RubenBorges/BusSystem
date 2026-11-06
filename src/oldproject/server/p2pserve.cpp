#include <web/server.hpp>
#include <web/client.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <thread>

//-----------------------------------------------
// Launches a background server coroutine for a simple peer-to-peer demo.
void ServerConnect_AsyncCoroutine() {
    boost::asio::io_context server_io(1);
    auto server = p2p::web::make_server(
        server_io, p2p::web::ServerOptions{5555, "/shutdown"});
    server->start();
    server_io.run();
}

// Connects a client coroutine to the demo server and sends a sample message.
void ClientConnect_AsyncCoroutine() {
    namespace asio = boost::asio;
    asio::io_context client_io(1);
    auto client_work = asio::make_work_guard(client_io);
    std::thread io_thread([&client_io]() {
        client_io.run();
    });

    p2p::web::AsyncEchoClient client(client_io);
    auto connect_future = asio::co_spawn(
        client_io,
        client.connect(p2p::web::ClientOptions{"127.0.0.1", 5555}),
        asio::use_future);
    connect_future.get();

    auto reply_future = asio::co_spawn(
        client_io,
        client.send_and_receive("hello"),
        asio::use_future);
    (void)reply_future.get();

    client.close();
    client_work.reset();
    client_io.stop();
    io_thread.join();
}

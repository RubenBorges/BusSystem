#include "messagebus/interfaces.hpp"

#include <web/client.hpp>
#include <web/server.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <cstring>
// POSIX and C headers required for your raw AF_UNIX socket operations
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <exception>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>

namespace messagebus {

// ==========================================
// StdIOInterfacer Implementation
// ==========================================

// Default constructor chains to standard console streams safely
StdIOInterfacer::StdIOInterfacer() : out_(&std::cout), in_(&std::cin) {}

// Parameterized constructor binds directly to your custom injected streams
StdIOInterfacer::StdIOInterfacer(std::ostream& out_stream, std::istream& in_stream) 
    : out_(&out_stream), in_(&in_stream) {}

void StdIOInterfacer::interface(const Message& message, std::expected<int, std::string>) {
  // Use the custom output stream pointer instead of hardcoded std::cout
  (*out_) << "StdIOInterfacer received message: " << message.content << std::endl;
}

// ==========================================
// PipedInterfacer Implementation
// ==========================================
PipedInterfacer::PipedInterfacer() = default;

void PipedInterfacer::interface(const Message& message, std::expected<int, std::string>) {
  std::cout << "PipedInterfacer received message: " << message.content << std::endl;
}

// ==========================================
// AF_UnixInterfacer Implementation
// ==========================================
AF_UnixInterfacer::AF_UnixInterfacer() : AF_UnixInterfacer("AF_Interface.Sock") {}

AF_UnixInterfacer::AF_UnixInterfacer(const std::string& path) 
    : server_fd(-1), socket_path(path) {
    
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("Failed to create AF_UNIX socket. File Descriptor still in invalid state");
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // Forces OS to remove any leftover stale socket file from a previous crash
    unlink(socket_path.c_str());

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to listen on socket");
    } 
} 

AF_UnixInterfacer::~AF_UnixInterfacer() {
    if (server_fd >= 0) {
        close(server_fd);
    }
    unlink(socket_path.c_str());
}

int AF_UnixInterfacer::getServerFD() const {
    return server_fd;
}

void AF_UnixInterfacer::interface(const Message& message, std::expected<int, std::string> status) {
    std::cout << "AF_UnixInterfacer received message: " << message.content << std::endl;
}
void AF_UnixInterfacer::stop() {
    running_.store(false, std::memory_order_release);
    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }
}

void AF_UnixInterfacer::listen_and_process_loop() {
    char buffer[1024];

    while (running_.load(std::memory_order_acquire)) {
        pollfd pfd{};
        pfd.fd = server_fd;
        pfd.events = POLLIN;

        const int ready = poll(&pfd, 1, 100);
        if (ready < 0) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        if (ready == 0) {
            continue;
        }

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        while (running_.load(std::memory_order_acquire)) {
            std::memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

            if (bytes_read > 0) {
                Message incomingMessage;
                incomingMessage.content = std::string(buffer, bytes_read);
                std::expected<int, std::string> status(static_cast<int>(bytes_read));
                this->interface(incomingMessage, status);
            } else {
                break; // Client disconnected or read error
            }
        }
        close(client_fd);
    }
}
// ==========================================
// NetworkInterfacer Implementation
// ==========================================
NetworkInterfacer::NetworkInterfacer() : NetworkInterfacer("127.0.0.1", 4545) {}

NetworkInterfacer::NetworkInterfacer(const std::string& ip, int port) : ip_(ip), port_(port) {}
void NetworkInterfacer::interface(const Message& message, std::expected<int, std::string>) {
  namespace asio = boost::asio;

  const unsigned short port = port_;
  auto ip = ip_;
  
  // 1. Initialize Server Layer Context
  asio::io_context server_io(1);
  auto server = p2p::web::make_server(server_io, p2p::web::ServerOptions{port, "/shutdown"});
  server->start();

  std::thread server_thread([&server_io]() {
    server_io.run();
  });

  // 2. Initialize Client Layer Context
  asio::io_context client_io(1);
  auto client_work = asio::make_work_guard(client_io);
  std::thread client_thread([&client_io]() {
    client_io.run();
  });

  try {
    p2p::web::AsyncEchoClient client(client_io);

    auto connect_future = asio::co_spawn(
        client_io,
        client.connect(p2p::web::ClientOptions{ip, port}),
        asio::use_future);
    connect_future.get();

    auto reply_future = asio::co_spawn(
        client_io,
        client.send_and_receive(message.content),
        asio::use_future);
    const std::string reply = reply_future.get();
    client.close();

    std::cout << "NetworkInterfacer echoed: " << reply << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << "NetworkInterfacer failed: " << ex.what() << std::endl;
  }

  // 3. Clean up Client context worker thread
  client_work.reset();
  client_io.stop();
  if (client_thread.joinable()) {
    client_thread.join();
  }

  // 4. CLEAN UP SERVER CONTEXT WORKER THREAD (FIXES THE HANG)
  // Request stop must be followed by explicitly stopping the io_context executor engine
  server->request_stop();
  server_io.stop(); // Forces any blocking background network epoll/kqueue polls to terminate!

  if (server_thread.joinable()) {
    server_thread.join(); // Unblocks instantly now
  }
}
}  // namespace messagebus

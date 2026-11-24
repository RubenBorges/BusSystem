module;

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

export module Graph:WebSocketConnection;

import std;
import :Wire;

export class Process;

export class WebSocketConnection {
public:
    using TLSContext = std::shared_ptr<boost::asio::ssl::context>;

    static std::shared_ptr<WebSocketConnection> connect(
        const std::string& host,
        const std::string& service,
        const std::string& target,
        TLSContext tlsContext);

    static std::shared_ptr<WebSocketConnection> accept(
        std::shared_ptr<boost::asio::io_context> ioContext,
        boost::asio::ip::tcp::socket socket,
        TLSContext tlsContext,
        const std::string& target = "/graph");

    ~WebSocketConnection();
    WebSocketConnection(const WebSocketConnection&) = delete;
    WebSocketConnection& operator=(const WebSocketConnection&) = delete;

    void sendFrame(const GraphFrame& frame);
    bool poll(Process& process);

private:
    struct Impl;
    explicit WebSocketConnection(std::unique_ptr<Impl> implementation);

    std::unique_ptr<Impl> implementation;
};

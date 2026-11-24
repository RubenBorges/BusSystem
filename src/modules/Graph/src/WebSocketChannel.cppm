export module Graph:WebSocketChannel;

import std;
import :Wire;
import :WebSocketConnection;

export class Process;

export class WebSocketChannel {
private:
    std::shared_ptr<WebSocketConnection> connection;

public:
    explicit WebSocketChannel(std::shared_ptr<WebSocketConnection> connection)
        : connection(std::move(connection)) {
        if (!this->connection) {
            throw std::invalid_argument("WebSocketChannel requires a connection");
        }
    }

    void send(const GraphFrame& frame) {
        connection->sendFrame(frame);
    }

    bool receive(Process& process) {
        return connection->poll(process);
    }
};

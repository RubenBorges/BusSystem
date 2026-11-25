module;

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

export module WebSocketConnection;

import std;
import WireProtocols;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using namespace protocol;

export class WebSocketConnection {
public:
    using TLSContext = std::shared_ptr<asio::ssl::context>;
    using WebSocketStream = websocket::stream<beast::ssl_stream<tcp::socket>>;

    struct SocketImplementation {
        explicit SocketImplementation(
            std::unique_ptr<WebSocketStream> websocket,
            TLSContext tlsContext,
            std::shared_ptr<asio::io_context> ioContext)
            : tlsContext(std::move(tlsContext)),
              ioContext(std::move(ioContext)),
              stream(std::move(websocket)) {}

        ~SocketImplementation() {
            stopping.store(true);
            boost::system::error_code ignored;
            if (stream) {
                stream->next_layer().next_layer().shutdown(tcp::socket::shutdown_both, ignored);
                stream->next_layer().next_layer().close(ignored);
            }
            if (reader.joinable()) {
                reader.join();
            }
        }

        void start() {
            reader = std::thread([this] { readLoop(); });
        }

        void readLoop() noexcept {
            try {
                beast::flat_buffer buffer;
                while (!stopping.load()) {
                    stream->read(buffer);
                    if (!stream->got_binary()) {
                        throw std::runtime_error("WebSocket graph connections require binary frames");
                    }

                    std::vector<uint8_t> bytes(buffer.size());
                    asio::buffer_copy(asio::buffer(bytes), buffer.data());
                    buffer.consume(buffer.size());

                    GraphFrame frame = decodeFrame(bytes);
                    {
                        std::lock_guard lock(queueMutex);
                        receivedFrames.push_back(std::move(frame));
                    }
                }
            } catch (...) {
                if (!stopping.load()) {
                    std::lock_guard lock(queueMutex);
                    failure = std::current_exception();
                }
            }
        }

        void rethrowFailure() {
            std::exception_ptr currentFailure;
            {
                std::lock_guard lock(queueMutex);
                currentFailure = failure;
            }
            if (currentFailure) {
                std::rethrow_exception(currentFailure);
            }
        }

        TLSContext tlsContext;
        std::shared_ptr<asio::io_context> ioContext;
        std::unique_ptr<WebSocketStream> stream;
        std::thread reader;
        std::atomic<bool> stopping{false};
        std::mutex writeMutex;
        std::mutex queueMutex;
        std::deque<GraphFrame> receivedFrames;
        std::exception_ptr failure;
    };

    static std::shared_ptr<WebSocketConnection> connect(
        const std::string& host,
        const std::string& service,
        const std::string& target,
        TLSContext tlsContext);

    static std::shared_ptr<WebSocketConnection> accept(
        std::shared_ptr<asio::io_context> ioContext,
        tcp::socket socket,
        TLSContext tlsContext,
        const std::string& target = "/graph");

    ~WebSocketConnection();
    WebSocketConnection(const WebSocketConnection&) = delete;
    WebSocketConnection& operator=(const WebSocketConnection&) = delete;

    void sendFrame(const GraphFrame& frame);
    void drainFrames(std::deque<GraphFrame>& outFrames);

    template <typename ProcessT>
    bool poll(ProcessT& process) {
        std::deque<GraphFrame> frames;
        drainFrames(frames);
        for (auto& frame : frames) {
            process.receive(std::move(frame));
        }
        return !frames.empty();
    }

private:
    std::unique_ptr<SocketImplementation> implementation;
    explicit WebSocketConnection(std::unique_ptr<SocketImplementation> implementation);
};

module;

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

module WebSocketConnection;

import std;
import WireProtocols;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using namespace protocol;

WebSocketConnection::WebSocketConnection(std::unique_ptr<SocketImplementation> implementation)
    : implementation(std::move(implementation)) {
    this->implementation->start();
}

WebSocketConnection::~WebSocketConnection() = default;

std::shared_ptr<WebSocketConnection> WebSocketConnection::connect(
    const std::string& host,
    const std::string& service,
    const std::string& target,
    TLSContext tlsContext
) {
    if (!tlsContext) {
        throw std::invalid_argument("WebSocket client requires a TLS context");
    }

    auto ioContext = std::make_shared<asio::io_context>();
    tcp::resolver resolver(*ioContext);
    const auto endpoints = resolver.resolve(host, service);
    tcp::socket socket(*ioContext);
    asio::connect(socket, endpoints);

    auto stream = std::make_unique<WebSocketStream>(std::move(socket), *tlsContext);
    stream->read_message_max(MAX_FRAME_SIZE);
    stream->next_layer().set_verify_mode(asio::ssl::verify_peer);
    stream->next_layer().set_verify_callback(asio::ssl::host_name_verification(host));
    if (SSL_set_tlsext_host_name(stream->next_layer().native_handle(), host.c_str()) != 1) {
        throw std::runtime_error("Failed to configure TLS server name: " +
                                 std::to_string(ERR_get_error()));
    }
    stream->next_layer().handshake(asio::ssl::stream_base::client);
    stream->handshake(host + ":" + service, target);

    return std::shared_ptr<WebSocketConnection>(
        new WebSocketConnection(std::make_unique<SocketImplementation>(
            std::move(stream),
            std::move(tlsContext),
            std::move(ioContext)
        ))
    );
}

std::shared_ptr<WebSocketConnection> WebSocketConnection::accept(
    std::shared_ptr<asio::io_context> ioContext,
    tcp::socket socket,
    TLSContext tlsContext,
    const std::string& target
) {
    if (!ioContext || &socket.get_executor().context() != ioContext.get()) {
        throw std::invalid_argument("Accepted socket must use the supplied io_context");
    }
    if (!tlsContext) {
        throw std::invalid_argument("WebSocket server requires a TLS context");
    }

    auto stream = std::make_unique<WebSocketStream>(std::move(socket), *tlsContext);
    stream->read_message_max(MAX_FRAME_SIZE);
    stream->next_layer().set_verify_mode(
        asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert
    );
    stream->next_layer().handshake(asio::ssl::stream_base::server);

    X509* peerCertificate = SSL_get1_peer_certificate(stream->next_layer().native_handle());
    if (!peerCertificate) {
        throw std::runtime_error("WebSocket peer did not present a client certificate");
    }
    X509_free(peerCertificate);
    if (SSL_get_verify_result(stream->next_layer().native_handle()) != X509_V_OK) {
        throw std::runtime_error("WebSocket client certificate verification failed");
    }

    beast::flat_buffer requestBuffer;
    beast::http::request_parser<beast::http::empty_body> requestParser;
    requestParser.header_limit(16 * 1024);
    requestParser.body_limit(0);
    beast::http::read(stream->next_layer(), requestBuffer, requestParser);
    const auto request = requestParser.release();
    if (request.target() != target) {
        throw std::runtime_error("WebSocket request target does not match the configured route");
    }
    stream->accept(request);

    return std::shared_ptr<WebSocketConnection>(
        new WebSocketConnection(std::make_unique<SocketImplementation>(
            std::move(stream),
            std::move(tlsContext),
            std::move(ioContext)
        ))
    );
}

void WebSocketConnection::sendFrame(const GraphFrame& frame) {
    implementation->rethrowFailure();
    const auto bytes = encodeFrame(frame);
    std::lock_guard lock(implementation->writeMutex);
    implementation->rethrowFailure();
    implementation->stream->binary(true);
    implementation->stream->write(asio::buffer(bytes));
}

void WebSocketConnection::drainFrames(std::deque<GraphFrame>& outFrames) {
    {
        std::lock_guard lock(implementation->queueMutex);
        outFrames.swap(implementation->receivedFrames);
    }
    implementation->rethrowFailure();
}

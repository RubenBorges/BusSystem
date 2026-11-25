module;

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

export module Graph;
import std;


export using WaveID = std::uint32_t;
export using ID = std::uint32_t;
export using Message = std::vector<std::uint8_t>;

export enum class FrameKind : std::uint8_t {
    Data,
    Acknowledgement,
    Cleanup,
    Position
};

#pragma pack(push, 1)
export struct MessageHeader {
    std::uint32_t magic;
    std::uint32_t waveId;
    std::uint32_t senderId;      // Identifies the transmitting Process node
    std::uint8_t kind;
    std::uint32_t payloadLength;
};
#pragma pack(pop)

export using CRC32_t = std::uint32_t;
export inline constexpr std::size_t MAX_FRAME_PAYLOAD_SIZE = 16 * 1024 * 1024;

export struct GraphFrame {
    WaveID waveID;
    Message payload;
    FrameKind kind{FrameKind::Data};
    ID senderID{0};              // Defaulted so existing 3-field initializers still compile
};

export class Wire {
private:
    int socket_fd;
    std::vector<std::uint8_t> readBuffer;
    const std::uint32_t MAGIC_MARKER = 0x47525048;

    std::uint32_t compute_crc32(const std::uint8_t* data, std::size_t length) const {
        std::uint32_t crc = 0xFFFFFFFF;
        for (std::size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            }
        }
        return ~crc;
    }

public:
    explicit Wire(int fd);
    ~Wire();
    Wire(const Wire&) = delete;
    Wire& operator=(const Wire&) = delete;
    Wire(Wire&& other) noexcept;
    Wire& operator=(Wire&& other) noexcept;

    bool sendFrame(const GraphFrame& frame) const;

    template <typename ProcessNodeType>
    bool receiveAndParse(ProcessNodeType& processNode) {
        if (socket_fd < 0) {
            return false;
        }

        std::uint8_t tempStackBuffer[1024];
        const ssize_t bytesRead =
            recv(socket_fd, tempStackBuffer, sizeof(tempStackBuffer), MSG_DONTWAIT);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return false;
            }
            throw std::system_error(errno, std::generic_category(), "recv");
        }
        if (bytesRead == 0) {
            throw std::runtime_error("Peer closed the socket connection");
        }

        readBuffer.insert(readBuffer.end(), tempStackBuffer, tempStackBuffer + bytesRead);
        bool receivedFrame = false;
        while (readBuffer.size() >= sizeof(MessageHeader)) {
            MessageHeader header;
            std::memcpy(&header, readBuffer.data(), sizeof(MessageHeader));
            if (ntohl(header.magic) != MAGIC_MARKER) {
                readBuffer.clear();
                close(socket_fd);
                socket_fd = -1;
                return receivedFrame;
            }

            const std::uint32_t payloadLength = ntohl(header.payloadLength);
            if (payloadLength > MAX_FRAME_PAYLOAD_SIZE) {
                throw std::length_error("Received frame payload is too large");
            }
            const std::size_t totalFrameSize =
                sizeof(MessageHeader) + payloadLength + sizeof(CRC32_t);
            if (readBuffer.size() < totalFrameSize) {
                break;
            }

            const std::uint8_t* payloadStart = readBuffer.data() + sizeof(MessageHeader);
            Message payload(payloadStart, payloadStart + payloadLength);
            CRC32_t receivedCRC;
            std::memcpy(&receivedCRC, payloadStart + payloadLength, sizeof(CRC32_t));

            if (compute_crc32(
                    readBuffer.data(),
                    sizeof(MessageHeader) + payloadLength) == ntohl(receivedCRC) &&
                header.kind <= static_cast<std::uint8_t>(FrameKind::Position)) {
                processNode.receive(GraphFrame{
                    ntohl(header.waveId),
                    std::move(payload),
                    static_cast<FrameKind>(header.kind),
                    ntohl(header.senderId)
                });
                receivedFrame = true;
            }
            readBuffer.erase(readBuffer.begin(), readBuffer.begin() + totalFrameSize);
        }
        return receivedFrame;
    }
};

export class CentralCoordinatorIDGen {
private:
    std::uint16_t server_shift_value;
    std::atomic<std::uint16_t> sequence{0};
    static constexpr std::uint16_t SERVER_MASK = 0x0F;
    static constexpr std::uint16_t SEQUENCE_MASK = 0x0FFF;

public:
    explicit CentralCoordinatorIDGen(std::uint8_t server_id);
    std::uint16_t generate_id();
};

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

export struct Edge {
    ID source;
    ID destination;
    auto operator<=>(const Edge&) const = default;
};

export template <typename ProcessNode>
class PointerChannel {
private:
    ProcessNode* target_node;

public:
    explicit PointerChannel(ProcessNode* node) : target_node(node) {
        if (!target_node) {
            throw std::invalid_argument("PointerChannel target cannot be null");
        }
    }

    void send(const GraphFrame& frame) {
        target_node->receive(frame);
    }
};

export class SocketChannel {
private:
    std::shared_ptr<Wire> wire;

public:
    explicit SocketChannel(std::shared_ptr<Wire> connection)
        : wire(std::move(connection)) {
        if (!wire) {
            throw std::invalid_argument("SocketChannel requires a Wire connection");
        }
    }

    void send(const GraphFrame& frame) {
        if (!wire->sendFrame(frame)) {
            throw std::runtime_error("Failed to send frame on socket channel");
        }
    }

    bool receive(Process& process) {
        return wire->receiveAndParse(process);
    }
};

export using Channel = std::variant<
    PointerChannel<Process>,
    SocketChannel,
    WebSocketChannel>;

export class Process {
public:
    using DataHandler = std::function<void(const GraphFrame&)>;
    ID identity;

private:
    std::optional<ID> parent;
    std::vector<ID> children;
    std::vector<ID> peers;                                   // Remote root peers (parentNode <-> parentNode)
    std::vector<ID> knownProcesses;
    std::vector<Edge> knownChannels;
    std::unordered_map<ID, Channel> activeChannels;
    std::unordered_map<WaveID, ID> waveParent;               // Dynamic upstream return path per WaveID
    std::unordered_map<WaveID, std::size_t> expectedMessagesByWave;
    std::set<WaveID> activeWaves;
    std::set<WaveID> completedWaves;
    std::set<WaveID> cleanedWaves;
    DataHandler dataHandler_;

public:
    Process() = delete;
    explicit Process(ID id);
    ~Process() = default;

    [[nodiscard]] bool isRoot() const noexcept { return !parent.has_value(); }
    [[nodiscard]] bool hasPeers() const noexcept { return !peers.empty(); }
    void setDataHandler(DataHandler handler);
    void start(GraphFrame frame);
    void pollChannels();
    bool isWaveCompleted(WaveID waveID) const;
    bool isWaveCleaned(WaveID waveID) const;
    bool isProcessKnown(ID id) const;
    void position(ID id, const std::vector<Edge>& neighbours);
    void receive(GraphFrame frame);
    void registerChannel(ID neighbourID, Channel channelImplementation);
    void addChild(ID childID);
    void addChild(Process* child);
    void addPeer(ID peerID);                                 // Links another root Process as a peer gateway
    void setParent(ID parentID);
    std::uint8_t getActiveWaves() { return static_cast<std::uint8_t>(activeWaves.size()); }
    std::uint8_t getCompletedWaves() { return static_cast<std::uint8_t>(completedWaves.size()); }

private:
    void go(GraphFrame frame);
    void back(GraphFrame frame);
    void completeWave(WaveID waveID);
};

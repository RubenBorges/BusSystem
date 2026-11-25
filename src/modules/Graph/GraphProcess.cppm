module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module GraphProcess;
import WebSocketConnection;
import std;
import Wire;             // Transitively exports WireProtocols


using ID = protocol::ID;
using WaveID = protocol::WaveID;
using GraphFrame = protocol::GraphFrame;

export struct Edge {
    ID source;
    ID destination;
    auto operator<=>(const Edge&) const = default;
};

export template <typename GraphProcessNode>
class PointerChannel {
    GraphProcessNode* target_node;

public:
    explicit PointerChannel(GraphProcessNode* node) : target_node(node) {
        if (!target_node) {
            throw std::invalid_argument("PointerChannel target cannot be null");
        }
    }

    void send(const GraphFrame& frame) {
        target_node->receive(frame);
    }
};

export class SocketChannel {
    Wire wire;

public:
    explicit SocketChannel(int fd) : wire(fd) {
        if (fd < 0) {
            throw std::invalid_argument("SocketChannel requires a valid file descriptor");
        }
    }

    SocketChannel(const SocketChannel&) = delete;
    SocketChannel& operator=(const SocketChannel&) = delete;
    SocketChannel(SocketChannel&&) noexcept = default;
    SocketChannel& operator=(SocketChannel&&) noexcept = default;

    void send(const GraphFrame& frame) {
        wire.sendFrame(frame);
    }

    template <typename ProcessNodeType>
    bool receive(ProcessNodeType& process) {
        return wire.receiveAndParse(process);
    }
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
    template <typename ProcessT>
    bool receive(ProcessT& process) {
        return connection->poll(process);
    }
};

export class GraphProcess {
public:
    using DataHandler = std::function<void(const GraphFrame&)>;
    using Channel = std::variant<PointerChannel<GraphProcess>, SocketChannel, WebSocketChannel>;

    ID identity;

    GraphProcess() = delete;
    explicit GraphProcess(ID id);
    ~GraphProcess() = default;

    [[nodiscard]] bool isRoot() const noexcept { return !parent.has_value(); }
    [[nodiscard]] bool hasPeers() const noexcept { return !peers.empty(); }

    void setDataHandler(DataHandler handler);
    void start(GraphFrame frame);
    void pollChannels();

    [[nodiscard]] bool isWaveCompleted(WaveID waveID) const;
    [[nodiscard]] bool isWaveCleaned(WaveID waveID) const;
    [[nodiscard]] bool isProcessKnown(ID id) const;

    void position(ID id, const std::vector<Edge>& neighbours);
    void receive(GraphFrame frame);
    void registerChannel(ID neighbourID, Channel channelImplementation);

    void addChild(ID childID);
    void addChild(GraphProcess* child);
    void addPeer(ID peerID); // Links another root GraphProcess as a peer gateway
    void setParent(ID parentID);

    [[nodiscard]] std::uint8_t getActiveWaves() const noexcept {
        return static_cast<std::uint8_t>(activeWaves.size());
    }

    [[nodiscard]] std::uint8_t getCompletedWaves() const noexcept {
        return static_cast<std::uint8_t>(completedWaves.size());
    }

private:
    void go(GraphFrame frame);
    void back(GraphFrame frame);
    void completeWave(WaveID waveID);

    std::optional<ID> parent;
    std::vector<ID> children;
    std::vector<ID> peers; // Remote root peers (parentNode <-> parentNode)
    std::vector<ID> knownProcesses;
    std::vector<Edge> knownChannels;
    std::unordered_map<ID, Channel> activeChannels;
    std::unordered_map<WaveID, ID> waveParent; // Dynamic upstream return path per WaveID
    std::unordered_map<WaveID,ssize_t> expectedMessagesByWave;
    std::set<WaveID> activeWaves;
    std::set<WaveID> completedWaves;
    std::set<WaveID> cleanedWaves;
    DataHandler dataHandler_;
};

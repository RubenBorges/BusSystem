export module Graph:Process;

import std;
import :Wire;
import :WebSocketChannel;

export class Process;

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
    ID identity;

private:
    std::optional<ID> parent;
    std::vector<ID> children;
    std::vector<ID> knownProcesses;
    std::vector<Edge> knownChannels;
    std::unordered_map<ID, Channel> activeChannels;
    std::unordered_map<WaveID, std::size_t> expectedMessagesByWave;
    std::set<WaveID> activeWaves;
    std::set<WaveID> completedWaves;
    std::set<WaveID> cleanedWaves;

public:
    Process() = delete;
    explicit Process(ID id);
    ~Process() = default;

    void start(GraphFrame frame);
    void pollChannels();
    bool isWaveCompleted(WaveID waveID) const;
    bool isWaveCleaned(WaveID waveID) const;
    bool isProcessKnown(ID id) const;
    void position(ID id, std::vector<Edge> neighbours);
    void receive(GraphFrame frame);
    void registerChannel(ID neighbourID, Channel channelImplementation);
    void addChild(ID childID);
    void addChild(Process* child);
    void setParent(ID parentID);
    std::uint8_t getActiveWaves() { return static_cast<std::uint8_t>(activeWaves.size()); }
    std::uint8_t getCompletedWaves() { return static_cast<std::uint8_t>(completedWaves.size()); }

private:
    void go(GraphFrame frame);
    void back(GraphFrame frame);
    void completeWave(WaveID waveID);
};

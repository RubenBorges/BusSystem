export module GraphBridgeComponent;

import std;
import Components;
import DataBus;
import EventType;
import Graph;

export class GraphBridgeComponent final : public Component<> {
private:
    std::string subscriberId_;
    Process localProcess_;
    CentralCoordinatorIDGen idGen_;

    // Thread-local re-entrancy guard: safe when multiple threads call DataBus::publish()
    static inline thread_local bool publishingFromGraph_{false};

    struct PublishGuard {
        bool previous;
        PublishGuard() noexcept : previous(publishingFromGraph_) {
            publishingFromGraph_ = true;
        }
        ~PublishGuard() noexcept {
            publishingFromGraph_ = previous;
        }
    };

public:
    GraphBridgeComponent(DataBus& bus, std::string subscriberId, ID localNodeId, std::uint8_t serverId)
        : Component<>(bus),
          subscriberId_(std::move(subscriberId)),
          localProcess_(localNodeId),
          idGen_(serverId) {
        localProcess_.setDataHandler([this](const GraphFrame& frame) {
            onGraphFrameReceived(frame);
        });
    }

    ~GraphBridgeComponent() {
        // DataBus::unsubscribe blocks until any active callback finishes,
        // preventing use-after-free during destruction.
        // Unsubscribe every bridged event type here (or in stop()).
        // bus().unsubscribe<YourEventType>(subscriberId_);
    }

    // Register a local event type to be bridged out to the remote Graph
    template <typename Event>
    void bridgeEvent() {
        bus().subscribe<Event>(subscriberId_, [this](const Event& event) {
            if (publishingFromGraph_) {
                return; // Prevent echo loop back to Graph
            }
            forwardToGraph(event);
        });
    }

    template <typename Event>
    void unbridgeEvent() {
        bus().unsubscribe<Event>(subscriberId_);
    }

    // Connect to a remote or local peer Process via any Graph Channel variant
    void connectPeer(ID remotePeerId, Channel outboundChannel, bool isUpstreamParent = false) {
        if (isUpstreamParent) {
            localProcess_.setParent(remotePeerId);
        } else {
            localProcess_.addChild(remotePeerId);
        }
        localProcess_.registerChannel(remotePeerId, std::move(outboundChannel));
    }

    // Convenience helper to connect two in-memory GraphBridgeComponents
    static void bridgeInMemory(GraphBridgeComponent& parentBridge, GraphBridgeComponent& childBridge) {
        parentBridge.connectPeer(
            childBridge.localProcess_.identity,
            PointerChannel<Process>(&childBridge.localProcess_),
            /*isUpstreamParent=*/false
        );
        childBridge.connectPeer(
            parentBridge.localProcess_.identity,
            PointerChannel<Process>(&parentBridge.localProcess_),
            /*isUpstreamParent=*/true
        );
    }

    void poll() {
        localProcess_.pollChannels();
    }

    Process& process() noexcept { return localProcess_; }

private:
    template <typename Event>
    void forwardToGraph(const Event& event) {
        Message payload = serializeEvent(event);
        localProcess_.start(GraphFrame{
            .waveID = idGen_.generate_id(),
            .payload = std::move(payload),
            .kind = FrameKind::Data
        });
    }

    void onGraphFrameReceived(const GraphFrame& frame) {
        if (frame.kind != FrameKind::Data) {
            return;
        }

        // Decode frame.payload into concrete EventType(s) and publish to local DataBus.
        // PublishGuard ensures DataBus::publish() does not trigger forwardToGraph().
        PublishGuard guard;
        decodeAndPublish(frame.payload);
    }

    template <typename Event>
    static Message serializeEvent(const Event& event);

    void decodeAndPublish(const Message& payload);
};

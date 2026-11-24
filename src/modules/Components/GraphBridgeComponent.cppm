/*
* This GraphBridgeComponent is used to facilitate communication between two remote Databus processes.
* Two seperate GraphBridgeComponents are be linked together via a shared, bidirectional Process::Channel
* they can both use to propogate data and execution across the Databus processes.
* 
* Intended Databus-DistributedGraph Architecture (see README.md): 
*
*  \\==========\\     \\======\\           [LOCAL]                        [REMOTE]           \\======\\     \\==========\\
*  \\ComponentC\\ <== \\=Data=\\    \\================\\ <<       << \\================\\    \\=Data=\\ ==> \\ComponentZ\\
*  \\ComponentB\\<===>\\==Bus=\\<==>\\GraphBridgeComp1\\<<=Channel=>>\\GraphBridgeComp2\\<==>\\==Bus=\\<===>\\ComponentY\\
*  \\ComponentA\\ ==> \\===1==\\    \\================\\ >>       >> \\================\\    \\==2===\\ <== \\ComponentX\\
*  \\==========\\     \\======\\                                                             \\======\\     \\==========\\        
*        1)               2)                3)                4)              3)                  2)              1)
*
 *  1) Components access the local DataBus as subscribers and publishers.
 *  2) DataBus facilitates local data/message sharing and cross-component execution.
 *  3) GraphBridgeComponent acts as the local DataBus's outward-facing gateway to remote nodes.
 *  4) Bidirectional Channel bridges two GraphBridgeComponents, extending DataBus systems across process/network boundaries
*/

export module GraphBridgeComponent;

import std;
import Components;
import DataBus;
import EventType;
import Graph;
export class GraphBridgeComponent final : public Component<> {
private:
    using EventTag = std::uint64_t;
    using DecoderFn = std::function<void(std::span<const std::uint8_t>)>;
    using BinderFn = std::function<void()>;
    using UnbinderFn = std::function<void()>;

    struct EventBinding {
        EventTag tag;
        DecoderFn decoder;
        BinderFn bind;
        UnbinderFn unbind;
    };

    DataBus& bus_;
    std::string subscriberId_;
    Process localProcess_;
    CentralCoordinatorIDGen idGen_;
    std::unordered_map<EventTag, EventBinding> bindings_;
    bool running_{false};
    static inline thread_local const void* incomingEventPtr_{nullptr};

    struct PublishGuard {
        const void* previous;
        explicit PublishGuard(const void* ptr) noexcept : previous(incomingEventPtr_) {
            incomingEventPtr_ = ptr;
        }
        ~PublishGuard() noexcept {
            incomingEventPtr_ = previous;
        }
    };

    // static inline thread_local bool publishingFromGraph_{false};

    // struct PublishGuard {
    //     bool previous;
    //     PublishGuard() noexcept : previous(publishingFromGraph_) {
    //         publishingFromGraph_ = true;
    //     }
    //     ~PublishGuard() noexcept {
    //         publishingFromGraph_ = previous;
    //     }
    // };

    template <typename Event>
    static constexpr EventTag tagFor() noexcept {
        std::string_view name = typeid(Event).name();
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : name) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

public:
    GraphBridgeComponent(DataBus& bus, std::string subscriberId, ID localNodeId, std::uint8_t serverId)
        : Component<>(bus),
          bus_(bus),
          subscriberId_(std::move(subscriberId)),
          localProcess_(localNodeId),
          idGen_(serverId) {
        localProcess_.setDataHandler([this](const GraphFrame& frame) {
            onGraphFrameReceived(frame);
        });
    }

    ~GraphBridgeComponent() override {
        stop();
    }

    void start() override {
        if (running_) {
            return;
        }
        for (auto& [tag, binding] : bindings_) {
            binding.bind();
        }
        running_ = true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        for (auto& [tag, binding] : bindings_) {
            binding.unbind();
        }
        running_ = false;
    }

    template <typename Event>
    void bridgeEvent(
        std::function<Message(const Event&)> encode = defaultEncode<Event>,
        std::function<Event(std::span<const std::uint8_t>)> decode = defaultDecode<Event>) {
        const EventTag tag = tagFor<Event>();
    auto decoder = [this, decode = std::move(decode)](std::span<const std::uint8_t> body) {
            Event event = decode(body);
            PublishGuard guard(&event); // Only suppress THIS exact event instance from echoing
            bus_.publish(event);
        };

        auto bind = [this, tag, encode = std::move(encode)]() {
            bus_.subscribe<Event>(subscriberId_, [this, tag, encode](const Event& event) {
                if (incomingEventPtr_ == &event) {
                    return; // Suppress echo of the incoming event, allow new reply events!
                }
        // auto decoder = [this, decode = std::move(decode)](std::span<const std::uint8_t> body) {
        //     Event event = decode(body);
        //     bus_.publish(event);
        // };

        // auto bind = [this, tag, encode = std::move(encode)]() {
        //     bus_.subscribe<Event>(subscriberId_, [this, tag, encode](const Event& event) {
        //         if (publishingFromGraph_) {
        //             return;
        //         }
                Message body = encode(event);
                Message framed;
                framed.resize(sizeof(EventTag) + body.size());
                std::memcpy(framed.data(), &tag, sizeof(EventTag));
                if (!body.empty()) {
                    std::memcpy(framed.data() + sizeof(EventTag), body.data(), body.size());
                }

                localProcess_.start(GraphFrame{
                    .waveID = idGen_.generate_id(),
                    .payload = std::move(framed),
                    .kind = FrameKind::Data
                });
            });
        };

        auto unbind = [this]() {
            bus_.unsubscribe<Event>(subscriberId_);
        };

        if (running_) {
            if (auto existing = bindings_.find(tag); existing != bindings_.end()) {
                existing->second.unbind();
            }
            bind();
        }

        bindings_[tag] = EventBinding{
            .tag = tag,
            .decoder = std::move(decoder),
            .bind = std::move(bind),
            .unbind = std::move(unbind)
        };
    }

    template <typename Event>
    void unbridgeEvent() {
        const EventTag tag = tagFor<Event>();
        if (auto it = bindings_.find(tag); it != bindings_.end()) {
            if (running_) {
                it->second.unbind();
            }
            bindings_.erase(it);
        }
    }

    void connectPeer(ID remotePeerId, Channel outboundChannel, bool isUpstreamParent = false) {
        if (isUpstreamParent) {
            localProcess_.setParent(remotePeerId);
        } else {
            localProcess_.addChild(remotePeerId);
        }
        localProcess_.registerChannel(remotePeerId, std::move(outboundChannel));
    }
    static void bridgeInMemory(GraphBridgeComponent& bridgeA, GraphBridgeComponent& bridgeB) {
        bridgeA.connectPeer(
            bridgeB.localProcess_.identity,
            PointerChannel<Process>(&bridgeB.localProcess_),
            /*isUpstreamParent=*/false
        );
        bridgeB.connectPeer(
            bridgeA.localProcess_.identity,
            PointerChannel<Process>(&bridgeA.localProcess_),
            /*isUpstreamParent=*/false
        );
    }
    // static void bridgeInMemory(GraphBridgeComponent& parentBridge, GraphBridgeComponent& childBridge) {
    //     parentBridge.connectPeer(
    //         childBridge.localProcess_.identity,
    //         PointerChannel<Process>(&childBridge.localProcess_),
    //         /*isUpstreamParent=*/false
    //     );
    //     childBridge.connectPeer(
    //         parentBridge.localProcess_.identity,
    //         PointerChannel<Process>(&parentBridge.localProcess_),
    //         /*isUpstreamParent=*/true
    //     );
    // }

    void poll() {
        localProcess_.pollChannels();
    }

    Process& process() noexcept { return localProcess_; }

private:
    void onGraphFrameReceived(const GraphFrame& frame) {
        if (!running_ || frame.kind != FrameKind::Data) {
            return;
        }
      //  PublishGuard guard;
        decodeAndPublish(frame.payload);
    }

    void decodeAndPublish(const Message& payload) {
        if (payload.size() < sizeof(EventTag)) {
            return;
        }

        EventTag tag{};
        std::memcpy(&tag, payload.data(), sizeof(EventTag));

        auto it = bindings_.find(tag);
        if (it == bindings_.end()) {
            return;
        }

        std::span<const std::uint8_t> body(
            payload.data() + sizeof(EventTag),
            payload.size() - sizeof(EventTag)
        );
        it->second.decoder(body);
    }

    template <typename Event>
    static Message defaultEncode(const Event& event) {
        static_assert(std::is_trivially_copyable_v<Event>,
            "Non-trivially copyable events require custom encode/decode lambdas in bridgeEvent()");
        Message bytes(sizeof(Event));
        std::memcpy(bytes.data(), &event, sizeof(Event));
        return bytes;
    }

    template <typename Event>
    static Event defaultDecode(std::span<const std::uint8_t> bytes) {
        static_assert(std::is_trivially_copyable_v<Event>,
            "Non-trivially copyable events require custom encode/decode lambdas in bridgeEvent()");
        if (bytes.size() != sizeof(Event)) {
            throw std::runtime_error("Invalid payload size for bridged event");
        }
        Event event{};
        std::memcpy(&event, bytes.data(), sizeof(Event));
        return event;
    }
};

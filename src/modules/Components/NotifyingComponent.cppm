export module NotifyingComponent;

import std;
import Components;
import EventType;
import Graph;

export class NotifyingComponent final : public Component<> {
public:
    explicit NotifyingComponent(DataBus& bus) : Component<>(bus) {}

    NotifyingComponent(DataBus& bus, Process& graphProcess)
        : Component<>(bus), graphProcess_(&graphProcess) {}

    void start() override {
        if (started_) {
            return;
        }

        bus_.subscribe<NotifyEvent>(
            SubscriptionId,
            [this](const NotifyEvent& event) { notify(event); });

        try {
            if (graphProcess_) {
                graphProcess_->setDataHandler(
                    [this](const GraphFrame& frame) { receive(frame); });
            }
            started_ = true;
        } catch (...) {
            bus_.unsubscribe<NotifyEvent>(SubscriptionId);
            throw;
        }
    }

    ~NotifyingComponent() override {
        if (started_) {
            stop();
        }
    }

    void stop() override {
        if (!started_) {
            return;
        }

        bus_.unsubscribe<NotifyEvent>(SubscriptionId);
        if (graphProcess_) {
            std::lock_guard lock(graphMutex_);
            graphProcess_->setDataHandler({});
        }
        started_ = false;
    }

    void pollGraph() {
        if (!graphProcess_) {
            throw std::logic_error("Graph transport is not configured");
        }

        std::lock_guard lock(graphMutex_);
        graphProcess_->pollChannels();
    }

private:
    static constexpr std::string_view SubscriptionId{"notifying-component"};
    static constexpr std::array<std::uint8_t, 4> PayloadMagic{'N', 'T', 'F', 'Y'};
    static constexpr std::uint8_t PayloadVersion{1};
    static constexpr std::size_t PayloadHeaderSize{18};

    static constexpr std::string_view typeName(NotifyEvent::Type type) {
        switch (type) {
        case NotifyEvent::Type::None: return "NONE";
        case NotifyEvent::Type::Note: return "NOTE";
        case NotifyEvent::Type::Warning: return "WARNING";
        case NotifyEvent::Type::Error: return "ERROR";
        case NotifyEvent::Type::Critical: return "CRITICAL";
        case NotifyEvent::Type::Sys: return "SYSTEM";
        }
        return "UNKNOWN";
    }

    static bool isValidType(NotifyEvent::Type type) {
        switch (type) {
        case NotifyEvent::Type::None:
        case NotifyEvent::Type::Note:
        case NotifyEvent::Type::Warning:
        case NotifyEvent::Type::Error:
        case NotifyEvent::Type::Critical:
        case NotifyEvent::Type::Sys:
            return true;
        }
        return false;
    }

    static void appendU32(Message& payload, std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            payload.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    static void appendU64(Message& payload, std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            payload.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    static std::uint32_t readU32(const Message& payload, std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            value = (value << 8) | payload[offset + i];
        }
        return value;
    }

    static std::uint64_t readU64(const Message& payload, std::size_t offset) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            value = (value << 8) | payload[offset + i];
        }
        return value;
    }

    static Message encode(const NotifyEvent& event) {
        if (!isValidType(event.type)) {
            throw std::invalid_argument("Cannot transmit a notification with an invalid type");
        }
        if (event.message.size() > std::numeric_limits<std::uint32_t>::max() ||
            event.message.size() > MAX_FRAME_PAYLOAD_SIZE - PayloadHeaderSize) {
            throw std::length_error("Notification is too large to transmit over Graph");
        }

        const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            event.now.time_since_epoch()).count();
        Message payload;
        payload.reserve(PayloadHeaderSize + event.message.size());
        payload.insert(payload.end(), PayloadMagic.begin(), PayloadMagic.end());
        payload.push_back(PayloadVersion);
        payload.push_back(static_cast<std::uint8_t>(event.type));
        appendU64(payload, std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(timestamp)));
        appendU32(payload, static_cast<std::uint32_t>(event.message.size()));
        for (const unsigned char byte : event.message) {
            payload.push_back(byte);
        }
        return payload;
    }

    static std::optional<NotifyEvent> decode(const GraphFrame& frame) {
        const Message& payload = frame.payload;
        if (payload.size() < PayloadMagic.size() ||
            !std::equal(PayloadMagic.begin(), PayloadMagic.end(), payload.begin())) {
            return std::nullopt;
        }
        if (payload.size() < PayloadHeaderSize) {
            throw std::invalid_argument("Malformed Graph notification payload");
        }
        if (payload[4] != PayloadVersion) {
            throw std::invalid_argument("Unsupported Graph notification payload version");
        }

        const auto type = static_cast<NotifyEvent::Type>(payload[5]);
        if (!isValidType(type)) {
            throw std::invalid_argument("Graph notification contains an invalid type");
        }
        const std::uint32_t messageSize = readU32(payload, 14);
        if (messageSize != payload.size() - PayloadHeaderSize) {
            throw std::invalid_argument("Malformed Graph notification message length");
        }

        const auto timestamp = std::bit_cast<std::int64_t>(readU64(payload, 6));
        const auto sinceEpoch = std::chrono::duration_cast<
            std::chrono::system_clock::duration>(
                std::chrono::microseconds{timestamp});
        return NotifyEvent{
            .type = type,
            .now = std::chrono::system_clock::time_point{sinceEpoch},
            .message = std::string(
                reinterpret_cast<const char*>(payload.data() + PayloadHeaderSize),
                messageSize)
        };
    }

    void notify(const NotifyEvent& event) {
        print(event);
        if (!graphProcess_ || !graphProcess_->isRoot()) {
            return;
        }

        std::lock_guard lock(graphMutex_);
        if (nextWaveID_ == 0) {
            ++nextWaveID_;
        }
        graphProcess_->start(GraphFrame{
            nextWaveID_++,
            encode(event),
            FrameKind::Data
        });
    }

    void receive(const GraphFrame& frame) {
        if (frame.kind != FrameKind::Data) {
            return;
        }
        if (const auto event = decode(frame)) {
            print(*event);
        }
    }

    void print(const NotifyEvent& event) {
        std::lock_guard lock(outputMutex_);
        std::ostream& output =
            event.type == NotifyEvent::Type::Warning ||
                    event.type == NotifyEvent::Type::Error ||
                    event.type == NotifyEvent::Type::Critical
                ? std::cerr
                : std::cout;
        output << std::format(
            "[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
            event.now,
            typeName(event.type),
            event.message) << std::flush;
        if (!output) {
            throw std::runtime_error("Could not print notification to the terminal");
        }
    }

    Process* graphProcess_{nullptr};
    std::mutex outputMutex_;
    std::mutex graphMutex_;
    WaveID nextWaveID_{1};
};
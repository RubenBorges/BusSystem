module;

#include <cstdio>

export module NotifyingComponent;

import std;
import Components;
import EventType;
import Graph;

export class NotifyingComponent final : public Component<> {
public:
    explicit NotifyingComponent(DataBus& bus) : Component<>(bus) {}

    NotifyingComponent(DataBus& bus, Process& graphProcess, std::uint8_t serverId = 0)
        : Component<>(bus),
          graphProcess_(&graphProcess),
          idGen_(serverId) {}

    void start() override {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        char idBuf[48];
        const std::string_view subId = formatSubId(idBuf);

       bus_.subscribe<NotifyEvent>(
            subId,
            [this](const NotifyEvent& event) { notify(event); });

        try {
            if (graphProcess_) {
                std::lock_guard lock(graphMutex_);
                graphProcess_->setDataHandler(
                    [this](const GraphFrame& frame) { receive(frame); });
            }
        } catch (...) {
            started_.store(false, std::memory_order_release);
            bus_.unsubscribe<NotifyEvent>(subId);
            throw;
        }
    }

    ~NotifyingComponent() override {
        stop();
    }

    void stop() override {
        if (!started_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        char idBuf[48];
        bus_.unsubscribe<NotifyEvent>(formatSubId(idBuf));

        if (graphProcess_) {
            std::lock_guard lock(graphMutex_);
            graphProcess_->setDataHandler({});
        }
    }

    void pollGraph() {
        if (!graphProcess_) {
            throw std::logic_error("Graph transport is not configured");
        }

        std::lock_guard lock(graphMutex_);
        graphProcess_->pollChannels();
    }

    static Message encode(const NotifyEvent& event) {
        if (!isValidType(event.type)) {
            throw std::invalid_argument("Cannot transmit a notification with an invalid type");
        }
        if (event.message.size() > std::numeric_limits<std::uint32_t>::max() ||
            event.message.size() > MAX_FRAME_PAYLOAD_SIZE - PayloadHeaderSize) {
            throw std::length_error("Notification is too large to transmit over Graph");
        }

        const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            event.now.time_since_epoch()).count();

        Message payload;
        payload.reserve(PayloadHeaderSize + event.message.size());
        payload.append_range(PayloadMagic);
        payload.push_back(PayloadVersion);
        payload.push_back(static_cast<std::uint8_t>(event.type));
        appendU64(payload, std::bit_cast<std::uint64_t>(static_cast<std::int64_t>(timestamp)));
        appendU32(payload, static_cast<std::uint32_t>(event.message.size()));

        const auto msgBytes = std::as_bytes(std::span{event.message});
        payload.append_range(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(msgBytes.data()),
            msgBytes.size()
        });
        return payload;
    }

    static std::optional<NotifyEvent> decodePayload(std::span<const std::uint8_t> payload) {
        if (payload.size() < PayloadMagic.size() ||
            !std::ranges::equal(PayloadMagic, payload.first<PayloadMagic.size()>())) {
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
        const auto sinceEpoch = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds{timestamp});

        return NotifyEvent{
            .type = type,
            .now = std::chrono::system_clock::time_point{sinceEpoch},
            .message = std::string(
                reinterpret_cast<const char*>(payload.data() + PayloadHeaderSize),
                messageSize)
        };
    }

    static std::optional<NotifyEvent> decode(const GraphFrame& frame) {
        return decodePayload(frame.payload);
    }

private:
    static inline std::atomic<std::uint64_t> nextInstanceId_{1};
    static constexpr std::array<std::uint8_t, 4> PayloadMagic{'N', 'T', 'F', 'Y'};
    static constexpr std::uint8_t PayloadVersion{1};
    static constexpr std::size_t PayloadHeaderSize{18};

    template <std::size_t N>
    [[nodiscard]] std::string_view formatSubId(char (&buf)[N]) const noexcept {
        const auto res = std::format_to_n(buf, N, "notifying-component-{}", instanceId_);
        return {buf, static_cast<std::size_t>(res.out - buf)};
    }

    static constexpr std::string_view typeName(NotifyEvent::Type type) noexcept {
        switch (type) {
        case NotifyEvent::Type::None:     return "NONE";
        case NotifyEvent::Type::Note:     return "NOTE";
        case NotifyEvent::Type::Warning:  return "WARNING";
        case NotifyEvent::Type::Error:    return "ERROR";
        case NotifyEvent::Type::Critical: return "CRITICAL";
        case NotifyEvent::Type::Sys:      return "SYSTEM";
        }
        return "UNKNOWN";
    }

    static constexpr bool isValidType(NotifyEvent::Type type) noexcept {
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
        if constexpr (std::endian::native == std::endian::little) {
            value = std::byteswap(value);
        }
        payload.append_range(std::bit_cast<std::array<std::uint8_t, sizeof(value)>>(value));
    }

    static void appendU64(Message& payload, std::uint64_t value) {
        if constexpr (std::endian::native == std::endian::little) {
            value = std::byteswap(value);
        }
        payload.append_range(std::bit_cast<std::array<std::uint8_t, sizeof(value)>>(value));
    }

    static constexpr std::uint32_t readU32(std::span<const std::uint8_t> payload, std::size_t offset) noexcept {
        std::array<std::uint8_t, sizeof(std::uint32_t)> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = payload[offset + i];
        }
        auto value = std::bit_cast<std::uint32_t>(bytes);
        if constexpr (std::endian::native == std::endian::little) {
            value = std::byteswap(value);
        }
        return value;
    }

    static constexpr std::uint64_t readU64(std::span<const std::uint8_t> payload, std::size_t offset) noexcept {
        std::array<std::uint8_t, sizeof(std::uint64_t)> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = payload[offset + i];
        }
        auto value = std::bit_cast<std::uint64_t>(bytes);
        if constexpr (std::endian::native == std::endian::little) {
            value = std::byteswap(value);
        }
        return value;
    }

    void notify(const NotifyEvent& event) {
        if (!started_.load(std::memory_order_acquire)) {
            return;
        }

        print(event);
        if (!graphProcess_ || !graphProcess_->isRoot()) {
            return;
        }

        std::lock_guard lock(graphMutex_);
        if (!started_.load(std::memory_order_acquire)) {
            return;
        }
        graphProcess_->start(GraphFrame{
            idGen_.generate_id(),
            encode(event),
            FrameKind::Data
        });
    }

    void receive(const GraphFrame& frame) {
        if (!started_.load(std::memory_order_acquire) || frame.kind != FrameKind::Data) {
            return;
        }
        try {
            if (const auto event = decode(frame)) {
                print(*event);
            }
        } catch (const std::exception& ex) {
            std::println(stderr, "[NotifyingComponent] Dropping malformed frame: {}", ex.what());
        }
    }

    void print(const NotifyEvent& event) {
        std::lock_guard lock(outputMutex_);
        std::FILE* target =
            (event.type == NotifyEvent::Type::Warning ||
             event.type == NotifyEvent::Type::Error ||
             event.type == NotifyEvent::Type::Critical)
                ? stderr
                : stdout;

        std::println(target, "[{:%Y-%m-%d %H:%M:%S}] [{}] {}",
                     event.now, typeName(event.type), event.message);
        if (std::fflush(target) != 0 || std::ferror(target) != 0) {
            throw std::runtime_error("Could not print notification to the terminal");
        }
    }

    // Members ordered by alignment/size to minimize struct padding
    std::mutex outputMutex_;
    std::mutex graphMutex_;
    Process* graphProcess_{nullptr};
    std::uint64_t instanceId_{nextInstanceId_.fetch_add(1, std::memory_order_relaxed)};
    CentralCoordinatorIDGen idGen_{0};
    std::atomic<bool> started_{false};
};

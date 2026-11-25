module;

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

export module WireProtocols;

import std;

export namespace protocol {

    using WaveID = std::uint32_t;
    using ID = std::uint32_t;
    using Message = std::vector<std::uint8_t>;
    using CRC32_t = std::uint32_t;

    enum class FrameKind : std::uint8_t {
        Data = 0,
        Go = 1,
        Back = 2,
        Position = 3,
        Acknowledgement = 4,
        Cleanup = 5
    };

#pragma pack(push, 1)
    struct MessageHeader {
        std::uint32_t magic;
        std::uint32_t waveId;
        std::uint32_t senderId; // Identifies the transmitting GraphProcess node
        std::uint8_t kind;
        std::uint32_t payloadLength;
    };
#pragma pack(pop)

    static_assert(sizeof(MessageHeader) == 17, "MessageHeader must be packed to 17 bytes");

    inline constexpr std::size_t MAX_FRAME_SIZE = 16 * 1024 * 1024;
    inline constexpr std::size_t MAX_PAYLOAD_SIZE = MAX_FRAME_SIZE - sizeof(MessageHeader) - sizeof(CRC32_t);
    inline constexpr std::size_t MAX_FRAME_PAYLOAD_SIZE = MAX_PAYLOAD_SIZE;
    inline constexpr std::uint32_t MAGIC_MARKER = 0x47525048;

    struct GraphFrame {
        WaveID waveID{};
        ID senderID{};
        Message payload;
        FrameKind kind{FrameKind::Data};
    };

    inline CRC32_t compute_crc32(const std::uint8_t* data, std::size_t length) noexcept {
        CRC32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

    inline std::vector<std::uint8_t> encodeFrame(const GraphFrame& frame) {
        if (frame.payload.size() > MAX_PAYLOAD_SIZE) {
            throw std::length_error("GraphFrame payload exceeds MAX_PAYLOAD_SIZE");
        }

        MessageHeader header{};
        header.magic = htonl(MAGIC_MARKER);
        header.waveId = htonl(frame.waveID);
        header.senderId = htonl(frame.senderID);
        header.kind = static_cast<std::uint8_t>(frame.kind);
        header.payloadLength = htonl(static_cast<std::uint32_t>(frame.payload.size()));

        const std::size_t totalSize = sizeof(MessageHeader) + frame.payload.size() + sizeof(CRC32_t);
        std::vector<std::uint8_t> buffer(totalSize);

        std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
        if (!frame.payload.empty()) {
            std::memcpy(buffer.data() + sizeof(MessageHeader), frame.payload.data(), frame.payload.size());
        }

        const CRC32_t crcNetwork = htonl(compute_crc32(buffer.data(), sizeof(MessageHeader) + frame.payload.size()));
        std::memcpy(buffer.data() + sizeof(MessageHeader) + frame.payload.size(), &crcNetwork, sizeof(CRC32_t));

        return buffer;
    }

    inline GraphFrame decodeFrame(const std::vector<std::uint8_t>& buffer) {
        if (buffer.size() < sizeof(MessageHeader) + sizeof(CRC32_t)) {
            throw std::runtime_error("Frame buffer smaller than minimum header + CRC size");
        }

        MessageHeader header{};
        std::memcpy(&header, buffer.data(), sizeof(MessageHeader));

        if (ntohl(header.magic) != MAGIC_MARKER) {
            throw std::runtime_error("Invalid frame magic marker");
        }

        const std::uint32_t payloadLength = ntohl(header.payloadLength);
        if (payloadLength > MAX_PAYLOAD_SIZE ||
            buffer.size() != sizeof(MessageHeader) + payloadLength + sizeof(CRC32_t)) {
            throw std::runtime_error("Invalid frame payload length");
        }

        CRC32_t receivedCRC{};
        std::memcpy(&receivedCRC, buffer.data() + sizeof(MessageHeader) + payloadLength, sizeof(CRC32_t));

        if (compute_crc32(buffer.data(), sizeof(MessageHeader) + payloadLength) != ntohl(receivedCRC)) {
            throw std::runtime_error("Frame CRC32 mismatch");
        }

        if (header.kind > static_cast<std::uint8_t>(FrameKind::Position)) {
            throw std::runtime_error("Invalid FrameKind value");
        }

        const auto* payloadStart = buffer.data() + sizeof(MessageHeader);
        return GraphFrame{
            ntohl(header.waveId),
            ntohl(header.senderId),
            Message(payloadStart, payloadStart + payloadLength),
            static_cast<FrameKind>(header.kind)
        };
    }

} // namespace protocol

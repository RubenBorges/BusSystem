module;

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

export module Graph:Wire;

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
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
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

        while (true) {
            if (readBuffer.size() < sizeof(MessageHeader)) {
                break;
            }

            MessageHeader header;
            std::memcpy(&header, readBuffer.data(), sizeof(MessageHeader));
            if (ntohl(header.magic) != MAGIC_MARKER) {
                std::cerr << "[Wire Exception] Stream out of sync! Invalid magic marker. Dropping link.\n";
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
            std::memcpy(
                &receivedCRC,
                payloadStart + payloadLength,
                sizeof(CRC32_t));

            if (compute_crc32(
                    readBuffer.data(),
                    sizeof(MessageHeader) + payloadLength) != ntohl(receivedCRC)) {
                std::cerr << "[Wire Error] Checksum verification mismatch! Frame discarded.\n";
            } else {
                if (header.kind > static_cast<std::uint8_t>(FrameKind::Position)) {
                    std::cerr << "[Wire Error] Invalid frame kind. Frame discarded.\n";
                    readBuffer.erase(
                        readBuffer.begin(),
                        readBuffer.begin() + totalFrameSize);
                    continue;
                }

                processNode.receive(GraphFrame{
                    ntohl(header.waveId),
                    std::move(payload),
                    static_cast<FrameKind>(header.kind)
                });
                receivedFrame = true;
            }

            readBuffer.erase(readBuffer.begin(), readBuffer.begin() + totalFrameSize);
        }
        return receivedFrame;
    }
};

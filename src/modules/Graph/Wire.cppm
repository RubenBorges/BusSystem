module;

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

export module Wire;
export import WireProtocols;
import std;

using namespace protocol;

export class Wire {
    using SocketType = int; 

    SocketType socket_fd;
    std::vector<std::uint8_t> readBuffer;
    
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
        const ssize_t bytesRead{recv(socket_fd, tempStackBuffer, sizeof(tempStackBuffer), MSG_DONTWAIT)};

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return false;
            throw std::system_error(errno, std::generic_category(), "recv");
        }
        if (bytesRead == 0) {throw std::runtime_error("Peer closed the socket connection");}

        readBuffer.insert(readBuffer.end(), tempStackBuffer, tempStackBuffer + bytesRead);
        bool receivedFrame = false;

        while (true) {
            if (readBuffer.size() < sizeof(protocol::MessageHeader)) break;

            protocol::MessageHeader header;
            std::memcpy(&header, readBuffer.data(), sizeof(header));
            if (ntohl(header.magic) != MAGIC_MARKER) {
                std::cerr << "[Wire Exception] Stream out of sync! Invalid magic marker. Dropping link.\n";
                readBuffer.clear();
                close(socket_fd);
                socket_fd = -1;
                return receivedFrame;
            }

            const std::uint32_t payloadLength = ntohl(header.payloadLength);

            if (payloadLength > MAX_FRAME_PAYLOAD_SIZE) {throw std::length_error("Received frame payload is too large");}

            const ssize_t totalFrameSize{sizeof(header) + payloadLength + sizeof(CRC32_t)};

            if (readBuffer.size() < totalFrameSize) break;

            const std::uint8_t* payloadStart = readBuffer.data() + sizeof(header);
            Message payload(payloadStart, payloadStart + payloadLength);

            CRC32_t receivedCRC;
            std::memcpy(
                &receivedCRC,
                payloadStart + payloadLength,
                sizeof(CRC32_t));

            if (protocol::compute_crc32(readBuffer.data(),sizeof(protocol::MessageHeader) + payloadLength) != ntohl(receivedCRC)) {
                std::cerr << "[Wire Error] Checksum verification mismatch! Frame discarded.\n";
            } else {
                if (header.kind > static_cast<std::uint8_t>(FrameKind::Position)) {
                    std::cerr << "[Wire Error] Invalid frame kind. Frame discarded.\n";
                    readBuffer.erase(readBuffer.begin(),readBuffer.begin() + totalFrameSize);
                    continue;
                }

                processNode.receive(GraphFrame{
    ntohl(header.waveId),
    ntohl(header.senderId),
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


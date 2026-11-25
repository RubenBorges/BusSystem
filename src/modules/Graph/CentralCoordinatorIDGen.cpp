module;

// Put any #include headers here (e.g., <cstdint>, <atomic>)
#include <cstdint>

module Graph;
import std;

// Your CentralCoordinatorIDGen::CentralCoordinatorIDGen(...) and generate_id() definitions below

CentralCoordinatorIDGen::CentralCoordinatorIDGen(std::uint8_t server_id) {
    if (server_id > SERVER_MASK) {
        throw std::invalid_argument("Server ID must be between 0 and 15 (4 bits max).");
    }
    server_shift_value = static_cast<std::uint16_t>(server_id) << 12;
}

std::uint16_t CentralCoordinatorIDGen::generate_id() {
    std::uint16_t current_seq = sequence.fetch_add(1, std::memory_order_relaxed);
    current_seq &= SEQUENCE_MASK;
    return server_shift_value | current_seq;
}

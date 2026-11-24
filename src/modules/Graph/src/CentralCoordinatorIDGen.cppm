export module Graph:CentralCoordinatorIDGen;

import std;

export class CentralCoordinatorIDGen {
private:
    std::uint16_t server_shift_value;
    std::atomic<std::uint16_t> sequence{0};

    static constexpr std::uint16_t SERVER_MASK = 0x0F;
    static constexpr std::uint16_t SEQUENCE_MASK = 0x0FFF;

public:
    explicit CentralCoordinatorIDGen(std::uint8_t server_id);
    std::uint16_t generate_id();
};

module; 

// #include <cassert>
// #include <cstdint>
// #include <variant>
// #include <string>
import std;
export module EventType;

export {
// Event sent by the sensor component.
struct TemperatureUpdated {
    float celsius;
};

// Request type used to ask about a system subsystem.
struct GetSystemHealth {
    int subsystemId;
};

// Response type returned by the diagnostics provider.
struct HealthReport {
    bool ok;
    std::string status;
};


struct DatabaseTx {
    enum class Mode   { Read, Write };
    enum class Status { Waiting, Fail, Success };

    Mode mode;
    Status status{Status::Waiting};
    std::variant<std::string,std::uint64_t> pgXid;
};
}

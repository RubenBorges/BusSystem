module; 

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

struct LogEntry {
    enum class Type : std::uint8_t {
        None = 0,
        Note = 1 << 0,
        Warning = 1 << 1,
        Error = 1 << 2,
        Critical = 1 << 3,
        Sys = 1 << 4
    };

    Type type{Type::None};
    std::chrono::time_point<std::chrono::system_clock> now;
    std::string message;
};

struct NotifyEvent{
    enum class Type : std::uint8_t {
        None = 0,
        Note = 1 << 0,
        Warning = 1 << 1,
        Error = 1 << 2,
        Critical = 1 << 3,
        Sys = 1 << 4
    };

    Type type{Type::None};
    std::chrono::time_point<std::chrono::system_clock> now;
    std::string message;
};
}
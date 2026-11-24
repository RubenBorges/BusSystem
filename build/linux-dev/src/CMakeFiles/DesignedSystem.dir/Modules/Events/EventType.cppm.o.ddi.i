# 0 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Events/EventType.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Events/EventType.cppm"
module;

import std;
export module EventType;

export {

struct TemperatureUpdated {
    float celsius;
};


struct GetSystemHealth {
    int subsystemId;
};


struct HealthReport {
    bool ok;
    std::string status;
};


struct DatabaseTx {
    enum class Mode { Read, Write };
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

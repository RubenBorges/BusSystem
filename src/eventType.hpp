#pragma once
#include <string>
// Event sent by the sensor component.
struct Event{};
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
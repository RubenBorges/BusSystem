module; // Global module fragment for headers needed by the implementation.

// #include <iostream>
// #include <random>
// #include <string>
// #include <utility>
import std;
import EventType;
import DataBus;

module Components;

// --- SensorComponent ---

void SensorComponent::start() {
    started_ = true;
}

void SensorComponent::stop() {
    started_ = false;
}

void SensorComponent::readSensor() {
    if (!started_) {
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, 100);

    bus_.publish(TemperatureUpdated{static_cast<float>(dist(gen))});
}

// --- DiagnosticsComponent ---

void DiagnosticsComponent::start() {
    if (started_) {
        return;
    }
    started_ = true;

    bus_.subscribe<TemperatureUpdated>(
        "diagnostics-temperature",
        [](const TemperatureUpdated& event) {
            std::cout << "[Diagnostics] Logged temp: "
                      << event.celsius << "C\n";
        });

    bus_.provide<GetSystemHealth, HealthReport>(
        [](const GetSystemHealth& request) {
            return HealthReport{
                true,
                "Subsystem " + std::to_string(request.subsystemId) + " nominal"};
        });
}

void DiagnosticsComponent::stop() {
    if (!started_) {
        return;
    }
    bus_.unsubscribe<TemperatureUpdated>("diagnostics-temperature");
    bus_.unprovide<GetSystemHealth>();
    started_ = false;
}

// --- DashboardComponent ---

void DashboardComponent::start() {
    started_ = true;
}

void DashboardComponent::stop() {
    started_ = false;
}

void DashboardComponent::refresh() {
    if (!started_) {
        return;
    }

    HealthReport report = bus_.call<HealthReport>(GetSystemHealth{42});
    std::cout << "[Dashboard] Health check: "
              << report.status << '\n';
}

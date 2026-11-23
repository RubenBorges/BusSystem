#include "messagebus.hpp"
#include <iostream>
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

class SensorComponent : public Component {
public:
    explicit SensorComponent(DataBus& bus) : bus_(bus) {}

    void start() override {
        started_ = true;
    }

    void stop() override {
        started_ = false;
    }

    void readSensor() {
        if (!started_) {
            return;
        }
        bus_.publish(TemperatureUpdated{24.5f});
    }

private:
    DataBus& bus_;
    bool started_ = false;
};

class DiagnosticsComponent : public Component {
public:
    explicit DiagnosticsComponent(DataBus& bus) : bus_(bus) {}

    void start() override {
        if (started_) {
            return;
        }
        started_ = true;

        // Listen for passive temperature events.
        bus_.subscribe<TemperatureUpdated>(
            "diagnostics-temperature",
            [](const TemperatureUpdated& event) {
                std::cout << "[Diagnostics] Logged temp: "
                          << event.celsius << "C\n";
            });

        // Register one request handler for GetSystemHealth.
        bus_.provide<GetSystemHealth, HealthReport>(
            [](const GetSystemHealth& request) {
                return HealthReport{
                    true,
                    "Subsystem " + std::to_string(request.subsystemId) + " nominal"};
            });
    }

    void stop() override {
        if (!started_) {
            return;
        }
        bus_.unsubscribe<TemperatureUpdated>("diagnostics-temperature");
        bus_.unprovide<GetSystemHealth>();
        started_ = false;
    }

private:
    DataBus& bus_;
    bool started_ = false;
};

class DashboardComponent : public Component {
public:
    explicit DashboardComponent(DataBus& bus) : bus_(bus) {}

    void start() override {
        started_ = true;
    }

    void stop() override {
        started_ = false;
    }

    void refresh() {
        if (!started_) {
            return;
        }
        // Call the provider without knowing which component registered it.
        HealthReport report = bus_.call<HealthReport>(
            GetSystemHealth{42});
        std::cout << "[Dashboard] Health check: "
                  << report.status << '\n';
    }

private:
    DataBus& bus_;
    bool started_ = false;
};

int main() {
    // The bus routes events and requests between independent components.
    DataBus bus;
    SensorComponent sensor(bus);
    DiagnosticsComponent diagnostics(bus);
    DashboardComponent dashboard(bus);

    // Start each component to install its subscriptions and providers.
    sensor.start();
    diagnostics.start();
    dashboard.start();

    // The sensor broadcasts an event without knowing its subscribers.
    sensor.readSensor();

    // The dashboard makes a request without knowing the provider component.
    dashboard.refresh();

    // Stop in reverse order before shutting down the application.
    dashboard.stop();
    diagnostics.stop();
    sensor.stop();

    // Stopped components no longer publish or call through the bus.
    sensor.readSensor();
    dashboard.refresh();

    return 0;
}
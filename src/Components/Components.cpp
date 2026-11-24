#include <eventType.hpp>
#include <iostream>
#include <random>
#include <Components.hpp>

    void SensorComponent::start()  {
        started_ = true;
    }

    void SensorComponent::stop()  {
        started_ = false;
    }

    void SensorComponent::readSensor() {
        if (!started_) {
            return;
        }
        
        //GET READING
        //currently producing random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(1, 100);

        //PUBLISH RESULTS
        bus_.publish(TemperatureUpdated{static_cast<float>(dist(gen))});
    }



    void DiagnosticsComponent::start()  {
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

    void DiagnosticsComponent::stop()  {
        if (!started_) {
            return;
        }
        bus_.unsubscribe<TemperatureUpdated>("diagnostics-temperature");
        bus_.unprovide<GetSystemHealth>();
        started_ = false;
    }



    void DashboardComponent::start()  {
        started_ = true;
    }

    void DashboardComponent::stop()  {
        started_ = false;
    }

    void DashboardComponent::refresh() {
        if (!started_) {
            return;
        }
        // Call the provider without knowing which component registered it.
        HealthReport report = bus_.call<HealthReport>(
            GetSystemHealth{42});
        std::cout << "[Dashboard] Health check: "
                  << report.status << '\n';
    }


#include <Policies.hpp>
#include <eventType.hpp>
#include <Bus.hpp>
#include <Components.hpp>
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
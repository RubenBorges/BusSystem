#pragma once
#include <eventType.hpp>
#include <Bus.hpp>
#include <Policies.hpp>
#include <string>
// DataBus stores heterogeneous event subscribers and request providers by type.
class Component {
public:
    // Start registrations and processing owned by a component.
    virtual void start() = 0;

    // Remove registrations and stop processing owned by a component.
    virtual void stop() = 0;

    // Virtual destruction ensures derived component state is released correctly.
    virtual ~Component() = default;
};

class SensorComponent : public Component {
public:
    explicit SensorComponent(DataBus& bus, std::string initParam = "") : bus_(bus) {}

    void start() override ;

    void stop() override ;
    void readSensor();

private:
    DataBus& bus_;
    bool started_ = false;
};

class DiagnosticsComponent : public Component {
public:
    explicit DiagnosticsComponent(DataBus& bus, std::string initParam = "") : bus_(bus) {}

    void start() override ;
    void stop() override ;

private:
    DataBus& bus_;
    bool started_ = false;
};

class DashboardComponent : public Component {
public:
    explicit DashboardComponent(DataBus& bus, std::string initParam = "") : bus_(bus) {}

    void start() override ;
 
    void stop() override ;

    void refresh();
private:
    DataBus& bus_;
    bool started_ = false;
};


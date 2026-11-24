# 0 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Components/Components.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Components/Components.cppm"
module;

import std;

export module Components;

export import DataBus;
export import EventType;

export class Component {
public:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~Component() = default;
};

export class SensorComponent : public Component {
public:
    explicit SensorComponent(DataBus& bus, std::string initParam = "")
        : bus_(bus), initParam_(std::move(initParam)) {}

    void start() override;
    void stop() override;
    void readSensor();

private:
    DataBus& bus_;
    std::string initParam_;
    bool started_ = false;
};

export class DiagnosticsComponent : public Component {
public:
    explicit DiagnosticsComponent(DataBus& bus, std::string initParam = "")
        : bus_(bus), initParam_(std::move(initParam)) {}

    void start() override;
    void stop() override;

private:
    DataBus& bus_;
    std::string initParam_;
    bool started_ = false;
};

export class DashboardComponent : public Component {
public:
    explicit DashboardComponent(DataBus& bus, std::string initParam = "")
        : bus_(bus), initParam_(std::move(initParam)) {}

    void start() override;
    void stop() override;
    void refresh();

private:
    DataBus& bus_;
    std::string initParam_;
    bool started_ = false;
};

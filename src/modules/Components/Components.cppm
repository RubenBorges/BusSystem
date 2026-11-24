module; // Global module fragment: only headers needed by the class declarations

import std;
import TaskPolicies;

export module Components;

export import DataBus;
export import EventType;

export template <typename Policy = TaskPolicy>
class Component {
public:
    using task_policy_type = Policy;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~Component() = default;

protected:
    template <typename Function, typename... Args>
    decltype(auto) executeTask(Function&& function, Args&&... args) const {
        return taskExecutor_.execute(
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }

    const Policy& taskPolicy() const noexcept {
        return taskExecutor_.policy();
    }

private:
    TaskExecutor<Policy> taskExecutor_{};
};

export class SensorComponent : public Component<> {
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

export class DiagnosticsComponent : public Component<> {
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

export class DashboardComponent : public Component<> {
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

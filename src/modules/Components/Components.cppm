module; // Global module fragment: only headers needed by the class declarations

import std;
import TaskPolicies;

export module Components;

export import DataBus;
export import EventType;

export class IComponent {
public:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~IComponent() = default;
};

export template <typename Policy = DefaultTaskPolicy>
class Component : public IComponent {
public:
    using task_policy_type = Policy;

    explicit Component(DataBus& bus, std::string initParam = "")
        : bus_(bus), initParam_(std::move(initParam)) {}

protected:
    template <typename Function, typename... Args>
    decltype(auto) executeTask(Function&& function, Args&&... args) const {
        return taskExecutor_.run(
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }

    const Policy& taskPolicy() const noexcept {
        return taskExecutor_;
    }

    DataBus& bus_;
    std::string initParam_;
    bool started_ = false;

    TaskExecutor<Policy> taskExecutor_{};
};

export template <typename Policy = DefaultTaskPolicy>
class SensorComponent : public Component<Policy> {
public:
    using Component<Policy>::Component;
    using active_task_type = decltype(
        std::declval<TaskExecutor<Policy>&>().run([] {}));

    void start() override {
        if (this->started_) {
            return;
        }
        this->started_ = true;
    }

    void stop() override {
        if (activeTask_) {
            if constexpr (requires { activeTask_->request_stop(); }) {
                activeTask_->request_stop();
            }
            activeTask_.reset();
        }
        this->started_ = false;
    }

    void readSensor() {
        if (!this->started_) {
            return;
        }

        activeTask_.reset();
        activeTask_.emplace(this->executeTask([this]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(1, 100);
            this->bus_.publish(
                TemperatureUpdated{static_cast<float>(dist(gen))});
        }));
    }

private:
    std::optional<active_task_type> activeTask_;
};

export template <typename Policy = DefaultTaskPolicy>
class DiagnosticsComponent : public Component<Policy> {
public:
    using Component<Policy>::Component;

    void start() override {
        if (this->started_) {
            return;
        }
        this->started_ = true;

        this->bus_.template subscribe<TemperatureUpdated>(
            "diagnostics-temperature",
            [](const TemperatureUpdated& event) {
                std::cout << "[Diagnostics] Logged temp: "
                          << event.celsius << "C\n";
            });

        this->bus_.template provide<GetSystemHealth, HealthReport>(
            [](const GetSystemHealth& request) {
                return HealthReport{
                    true,
                    "Subsystem " + std::to_string(request.subsystemId) + " nominal"};
            });
    }

    void stop() override {
        if (!this->started_) {
            return;
        }
        this->bus_.template unsubscribe<TemperatureUpdated>("diagnostics-temperature");
        this->bus_.template unprovide<GetSystemHealth>();
        this->started_ = false;
    }
};

export template <typename Policy = DefaultTaskPolicy>
class DashboardComponent : public Component<Policy> {
public:
    using Component<Policy>::Component;
    using active_task_type = decltype(
        std::declval<TaskExecutor<Policy>&>().run([] {}));

    void start() override {
        this->started_ = true;
    }

    void stop() override {
        if (activeTask_) {
            if constexpr (requires { activeTask_->request_stop(); }) {
                activeTask_->request_stop();
            }
            activeTask_.reset();
        }
        this->started_ = false;
    }

    void refresh() {
        if (!this->started_) {
            return;
        }

        activeTask_.reset();
        activeTask_.emplace(this->executeTask([this]() {
            HealthReport report =
                this->bus_.template call<HealthReport>(GetSystemHealth{42});
            std::cout << "[Dashboard] Health check: "
                      << report.status << '\n';
        }));
    }

private:
    std::optional<active_task_type> activeTask_;
};

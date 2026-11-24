export module Components;

import std;
export import TaskPolicies;
export import DataBus;
export import EventType;

export class IComponent {
public:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~IComponent() = default;
};

// Policy host. A Component is configured with
//   * ExecPolicy : how background work is launched (inline, thread, async, ...)
//   * ErrPolicy  : what happens to exceptions the executor intercepts
// Both default to the library defaults, so `Component<>` and `Component<P>` work.
export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class Component : public IComponent {
public:
    using exec_policy_type = ExecPolicy;
    using error_policy_type = ErrPolicy;
    using task_policy_type = ExecPolicy;   // kept for existing code
    using executor_type = TaskExecutor<ExecPolicy, ErrPolicy>;
    using active_task_type = typename executor_type::void_task_type;

    explicit Component(DataBus& bus, std::string initParam = "")
        : bus_(bus), initParam_(std::move(initParam)) {}

    [[nodiscard]] bool isStarted() const noexcept { return started_; }

protected:
    template <typename Function, typename... Args>
    [[nodiscard]] auto executeTask(Function&& function, Args&&... args) const {
        return taskExecutor_.run(
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }

    template <TaskHandle Task>
    auto collectTask(Task& task) const {
        return taskExecutor_.collect(task);
    }

    const ExecPolicy& taskPolicy() const noexcept {
        return taskExecutor_;
    }

    // --- Single "active task" slot shared by components that run one job at a time ---

    // Replaces the active task (waiting for / joining the previous one) and
    // launches `job` through the execution policy.
    template <typename Function>
    void launchActive(Function&& job) {
        static_assert(std::is_void_v<std::invoke_result_t<Function&>>,
                      "launchActive expects a void() job");
        activeTask_.reset();
        if (auto task = executeTask(std::forward<Function>(job))) {
            activeTask_.emplace(std::move(*task));
        }
    }

    // Requests cancellation and drops the active task. Errors raised inside it
    // are discarded; use collectActive() first if you need them.
    void stopActive() {
        if (activeTask_) {
            activeTask_->request_stop();
            activeTask_.reset();
        }
    }

    // Waits for the active task and reports its outcome through the error policy.
    std::expected<void, std::exception_ptr> collectActive() {
        if (!activeTask_) {
            return {};
        }
        auto outcome = taskExecutor_.collect(*activeTask_);
        activeTask_.reset();
        return outcome;
    }

    DataBus& bus_;
    std::string initParam_;
    bool started_ = false;

    [[no_unique_address]] executor_type taskExecutor_{};
    std::optional<active_task_type> activeTask_;
};

export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class SensorComponent : public Component<ExecPolicy, ErrPolicy> {
public:
    using Base = Component<ExecPolicy, ErrPolicy>;
    using Base::Base;

    void start() override {
        this->started_ = true;
    }

    void stop() override {
        this->stopActive();
        this->started_ = false;
    }

    void readSensor() {
        if (!this->started_) {
            return;
        }

        this->launchActive([this]() {
            thread_local std::mt19937 gen{std::random_device{}()};
            std::uniform_int_distribution<int> dist(1, 100);
            this->bus_.publish(
                TemperatureUpdated{static_cast<float>(dist(gen))});
        });
    }
};

export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class DiagnosticsComponent : public Component<ExecPolicy, ErrPolicy> {
public:
    using Base = Component<ExecPolicy, ErrPolicy>;
    using Base::Base;

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

export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class DashboardComponent : public Component<ExecPolicy, ErrPolicy> {
public:
    using Base = Component<ExecPolicy, ErrPolicy>;
    using Base::Base;

    void start() override {
        this->started_ = true;
    }

    void stop() override {
        this->stopActive();
        this->started_ = false;
    }

    void refresh() {
        if (!this->started_) {
            return;
        }

        this->launchActive([this]() {
            HealthReport report =
                this->bus_.template call<HealthReport>(GetSystemHealth{42});
            std::cout << "[Dashboard] Health check: "
                      << report.status << '\n';
        });
    }
};

module;

import std;
import Graph;

export module TaskPolicies;
// ============================================================================
//  Concepts
// ============================================================================

// A policy that can launch a callable and hand back a task handle.
export template <typename Policy, typename Function, typename... Args>
concept ExecutionPolicy = requires(const Policy& policy, Function&& function, Args&&... args) {
    policy.execute(std::forward<Function>(function), std::forward<Args>(args)...);
};

// What every task handle returned by an execution policy must offer.
export template <typename Task>
concept TaskHandle = requires(Task& task, const Task& constTask) {
    { constTask.is_ready() } -> std::convertible_to<bool>;
    { task.request_stop() } -> std::convertible_to<bool>;
    task.get();
};

// An execution policy a Component can be built on: it must be able to launch a
// plain void() callable. (Remote/generator policies below are different families.)
export template <typename Policy>
concept TaskLaunchPolicy = ExecutionPolicy<Policy, void (*)()>;

// An error policy is told about every exception the executor intercepts.
// It either rethrows (propagate), or returns (the executor reports the failure
// to the caller through std::expected).
export template <typename Policy>
concept ErrorHandlingPolicy = requires(const Policy& policy, std::exception_ptr error) {
    policy.handle_exception(error);
};

// ============================================================================
//  Error policies
// ============================================================================

export struct RethrowErrorPolicy {
    [[noreturn]] void handle_exception(std::exception_ptr error) const {
        std::rethrow_exception(std::move(error));
    }
};

export struct IgnoreErrorPolicy {
    void handle_exception(std::exception_ptr) const noexcept {}
};

// ============================================================================
//  Task handles
// ============================================================================

export template <typename Result>
struct ImmediateTask {
    Result value{};

    constexpr bool is_ready() const noexcept { return true; }
    constexpr bool request_stop() noexcept { return false; }
    constexpr Result get() && { return std::move(value); }
    constexpr const Result& get() const& { return value; }
};

template <>
struct ImmediateTask<void> {
    constexpr bool is_ready() const noexcept { return true; }
    constexpr bool request_stop() noexcept { return false; }
    constexpr void get() const noexcept {}
};

export template <typename Result>
struct MultiThreadTask {
    std::jthread worker;
    std::future<Result> result;

    [[nodiscard]] bool is_ready() const {
        return result.valid() &&
            result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    bool request_stop() noexcept {
        return worker.request_stop();
    }

    void join() {
        if (worker.joinable()) {
            worker.join();
        }
    }

    decltype(auto) get() {return result.get();}
};

// NOTE: the destructor of a future obtained from std::async blocks until the
// task has finished, so destroying (or replacing) an AsyncTask waits for it.
export template <typename Result>
struct AsyncTask {
    std::future<Result> result;

    [[nodiscard]] bool is_ready() const {
        return result.valid() &&
            result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    bool request_stop() noexcept { return false; }
    decltype(auto) get() { return result.get(); }
};

// ============================================================================
//  Execution policies (family 1: "run this callable")
// ============================================================================

// Runs the callable immediately on the calling thread.
export struct LocalThreadPolicy {
    template <typename Function, typename... Args>
    constexpr auto execute(Function&& function, Args&&... args) const {
        using Result = std::remove_cvref_t<std::invoke_result_t<Function, Args...>>;
        if constexpr (std::is_void_v<Result>) {
            std::invoke(std::forward<Function>(function), std::forward<Args>(args)...);
            return ImmediateTask<void>{};
        } else {
            return ImmediateTask<Result>{
                std::invoke(std::forward<Function>(function), std::forward<Args>(args)...)
            };
        }
    }
};

// Runs the callable on its own std::jthread. If the callable accepts a
// std::stop_token as its first parameter, it receives the thread's token.
export struct MultiThreadPolicy {
    template <typename Function, typename... Args>
    [[nodiscard]] auto execute(Function&& function, Args&&... args) const {
        using FunctionType = std::decay_t<Function>;
        using ArgumentsType = std::tuple<std::decay_t<Args>...>;
        using Result = decltype(invokeTask(
            std::declval<FunctionType&>(),
            std::declval<std::stop_token>(),
            std::declval<std::decay_t<Args>&&>()...));

        std::promise<Result> completion;
        auto result = completion.get_future();

        auto worker = std::jthread{
            [task = FunctionType(std::forward<Function>(function)),
             arguments = ArgumentsType(std::forward<Args>(args)...),
             completion = std::move(completion)](
                std::stop_token stopToken) mutable {
                try {
                    std::apply(
                        [&task, stopToken, &completion](auto&&... unpackedArgs) {
                            if constexpr (std::is_void_v<Result>) {
                                invokeTask(
                                    task,
                                    stopToken,
                                    std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                                completion.set_value();
                            } else {
                                completion.set_value(invokeTask(
                                    task,
                                    stopToken,
                                    std::forward<decltype(unpackedArgs)>(unpackedArgs)...));
                            }
                        },
                        std::move(arguments));
                } catch (...) {
                    completion.set_exception(std::current_exception());
                }
            }};

        return MultiThreadTask<Result>{
            .worker = std::move(worker),
            .result = std::move(result)
        };
    }

private:
    template <typename Function, typename... Args>
    static decltype(auto) invokeTask(
        Function& function,
        std::stop_token stopToken,
        Args&&... args) {
        if constexpr (std::invocable<Function&, std::stop_token, Args...>) {
            return std::invoke(
                function,
                stopToken,
                std::forward<Args>(args)...);
        } else {
            return std::invoke(
                function,
                std::forward<Args>(args)...);
        }
    }
};

// Runs the callable with std::async(std::launch::async, ...).
export struct AsyncPolicy {
    template <typename Function, typename... Args>
    [[nodiscard]] auto execute(Function&& function, Args&&... args) const {
        using Result = std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>;
        return AsyncTask<Result>{
            std::async(
                std::launch::async,
                std::forward<Function>(function),
                std::forward<Args>(args)...)
        };
    }
};

// ============================================================================
//  Other policy families (NOT interchangeable with the execution policies
//  above, so they are deliberately not TaskLaunchPolicy models)
// ============================================================================

// Identity "policy" for generator pipelines. It takes a generator, not a callable.
export struct GeneratorPolicy {
    template <typename Value>
    [[nodiscard]] std::generator<Value> execute(
        std::generator<Value> generator) const {
        return generator;
    }
};

export class RemoteTaskHandle {
public:
    RemoteTaskHandle(Process& process, WaveID waveID) noexcept
        : process_(&process), waveID_(waveID) {}

    [[nodiscard]] bool poll() {
        process_->pollChannels();
        return isCompleted();
    }

    [[nodiscard]] bool isCompleted() const {
        return process_->isWaveCompleted(waveID_);
    }

    [[nodiscard]] bool isCleaned() const {
        return process_->isWaveCleaned(waveID_);
    }

    [[nodiscard]] WaveID waveID() const noexcept {
        return waveID_;
    }

private:
    Process* process_;
    WaveID waveID_;
};

export struct RemoteTaskPolicy {
    [[nodiscard]] RemoteTaskHandle execute(Process& root, GraphFrame frame) const {
        const WaveID waveID = frame.waveID;
        root.start(std::move(frame));
        return RemoteTaskHandle{root, waveID};
    }

    [[nodiscard]] RemoteTaskHandle start(Process& root, GraphFrame frame) const {
        return execute(root, std::move(frame));
    }

    [[nodiscard]] RemoteTaskHandle broadcast(Process& root, GraphFrame frame) const {
        return execute(root, std::move(frame));
    }

    bool convergecast(RemoteTaskHandle& wave) const {
        return wave.poll();
    }
};

export template <typename Policy>
concept RemoteExecutionPolicy = requires(
    const Policy& policy, Process& root, GraphFrame frame, RemoteTaskHandle& wave) {
    { policy.start(root, std::move(frame)) } -> std::same_as<RemoteTaskHandle>;
    { policy.convergecast(wave) } -> std::convertible_to<bool>;
};

// ============================================================================
//  Defaults
// ============================================================================

export using TaskPolicy = LocalThreadPolicy;
export using DefaultTaskPolicy = LocalThreadPolicy;
export using DefaultErrorPolicy = RethrowErrorPolicy;

// ============================================================================
//  Executor: host class combining an execution policy and an error policy
// ============================================================================

export template <
    TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
    ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class TaskExecutor : public ExecPolicy, private ErrPolicy {
public:
    // The task handle type the execution policy returns for Function(Args...).
    template <typename Function, typename... Args>
    using launch_result_t = decltype(std::declval<const ExecPolicy&>().execute(
        std::declval<Function>(), std::declval<Args>()...));

    // Handle type for a void() job (what Component stores as its active task).
    using void_task_type = launch_result_t<void (*)()>;

    constexpr TaskExecutor() = default;

    constexpr explicit TaskExecutor(ExecPolicy exec, ErrPolicy err = {})
        : ExecPolicy(std::move(exec)), ErrPolicy(std::move(err)) {}

    // Launch. Exceptions thrown while launching go to the error policy; if it
    // returns (e.g. IgnoreErrorPolicy) the failure is reported in the result,
    // never by falling off the end of the function.
    template <typename Function, typename... Args>
        requires ExecutionPolicy<ExecPolicy, Function, Args...>
    [[nodiscard]] auto run(Function&& function, Args&&... args) const
        -> std::expected<launch_result_t<Function, Args...>, std::exception_ptr> {
        try {
            return ExecPolicy::execute(
                std::forward<Function>(function),
                std::forward<Args>(args)...);
        } catch (...) {
            std::exception_ptr error = std::current_exception();
            ErrPolicy::handle_exception(error);
            return std::unexpected(std::move(error));
        }
    }

    // Wait for a task and fetch its result. Exceptions raised *inside* the task
    // (which Multi/Async policies capture in the future) go through the same
    // error policy, so every execution policy has the same error contract.
    template <TaskHandle Task>
    auto collect(Task& task) const {
        using Value = std::remove_cvref_t<decltype(task.get())>;
        using Outcome = std::expected<Value, std::exception_ptr>;
        try {
            if constexpr (std::is_void_v<Value>) {
                task.get();
                return Outcome{};
            } else {
                return Outcome{task.get()};
            }
        } catch (...) {
            std::exception_ptr error = std::current_exception();
            ErrPolicy::handle_exception(error);
            return Outcome{std::unexpected(std::move(error))};
        }
    }

    [[nodiscard]] const ExecPolicy& policy() const noexcept {
        return *this;
    }
};

// ----------------------------------------------------------------------------
//  Compile-time checks that the built-in policies model the concepts
// ----------------------------------------------------------------------------

static_assert(TaskLaunchPolicy<LocalThreadPolicy>);
static_assert(TaskLaunchPolicy<MultiThreadPolicy>);
static_assert(TaskLaunchPolicy<AsyncPolicy>);
static_assert(!TaskLaunchPolicy<GeneratorPolicy>);
static_assert(!TaskLaunchPolicy<RemoteTaskPolicy>);
static_assert(RemoteExecutionPolicy<RemoteTaskPolicy>);
static_assert(ErrorHandlingPolicy<RethrowErrorPolicy>);
static_assert(ErrorHandlingPolicy<IgnoreErrorPolicy>);
static_assert(TaskHandle<ImmediateTask<void>>);
static_assert(TaskHandle<ImmediateTask<int>>);
static_assert(TaskHandle<MultiThreadTask<void>>);
static_assert(TaskHandle<AsyncTask<void>>);

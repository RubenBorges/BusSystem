export module TaskPolicies;

import std;
import Graph;

export struct LocalThreadPolicy {
    template <typename Function, typename... Args>
    constexpr decltype(auto) execute(Function&& function, Args&&... args) const
        noexcept(noexcept(std::invoke(
            std::forward<Function>(function),
            std::forward<Args>(args)...))) {
        return std::invoke(
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }
};

export template <typename Result>
struct MultiThreadTask {
    std::jthread worker;
    std::future<Result> result;

    bool request_stop() noexcept {
        return worker.request_stop();
    }

    void join() {
        worker.join();
    }

    decltype(auto) get() {
        return result.get();
    }
};

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

export struct AsyncPolicy {
    template <typename Function, typename... Args>
    [[nodiscard]] auto execute(Function&& function, Args&&... args) const {
        return std::async(
            std::launch::async,
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }
};

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

export using TaskPolicy = LocalThreadPolicy;

export template <typename Policy, typename... Args>
concept TaskExecutionPolicy = requires(
    const Policy& policy,
    Args&&... args) {
    policy.execute(std::forward<Args>(args)...);
};

export template <typename Policy = TaskPolicy>
class TaskExecutor {
public:
    template <typename Function, typename... Args>
        requires TaskExecutionPolicy<Policy, Function, Args...>
    decltype(auto) execute(Function&& function, Args&&... args) const {
        return policy_.execute(
            std::forward<Function>(function),
            std::forward<Args>(args)...);
    }

    const Policy& policy() const noexcept {
        return policy_;
    }

private:
    Policy policy_{};
};
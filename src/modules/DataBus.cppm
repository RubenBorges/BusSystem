module;

// #include <algorithm>
// #include <any>
// #include <functional>
// #include <mutex>
// #include <shared_mutex>
// #include <stdexcept>
// #include <string>
// #include <string_view>
// #include <typeindex>
// #include <unordered_map>
// #include <utility>
// #include <vector>

import std;

export module DataBus;

export class DataBus {
public:
    DataBus() = default;
    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    template <typename Event>
    void subscribe(std::string_view identifier, std::function<void(const Event&)> callback) {
        std::unique_lock lock(mtx_);
        subscribers_[typeid(Event)].push_back(
            NamedSubscriber{
                std::string{identifier},
                [callback = std::move(callback)](const std::any& data) {
                    callback(std::any_cast<const Event&>(data));
                }});
    }

    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        std::unique_lock lock(mtx_);
        auto found = subscribers_.find(typeid(Event));
        if (found == subscribers_.end()) {
            return;
        }

        std::erase_if(found->second, [identifier](const NamedSubscriber& subscriber) {
            return subscriber.identifier == identifier;
        });
    }

    template <typename Request>
    void unprovide() {
        std::unique_lock lock(mtx_);
        providers_.erase(typeid(Request));
    }

    template <typename Event>
    void publish(const Event& event) const {
        std::vector<NamedSubscriber> snapshot;
        {
            std::shared_lock lock(mtx_);
            auto found = subscribers_.find(typeid(Event));
            if (found == subscribers_.end()) {
                return;
            }
            snapshot = found->second;
        }

        for (const auto& subscriber : snapshot) {
            subscriber.callback(event);
        }
    }

    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler) {
        std::unique_lock lock(mtx_);
        providers_[typeid(Request)] =
            [handler = std::move(handler)](const std::any& request) -> std::any {
                return handler(std::any_cast<const Request&>(request));
            };
    }

    template <typename Response, typename Request>
    Response call(const Request& request) const {
        std::function<std::any(const std::any&)> handlerCopy;
        {
            std::shared_lock lock(mtx_);
            auto found = providers_.find(typeid(Request));
            if (found == providers_.end()) {
                throw std::runtime_error("No component registered to handle this call");
            }
            handlerCopy = found->second;
        }

        return std::any_cast<Response>(handlerCopy(request));
    }

private:
    struct NamedSubscriber {
        std::string identifier;
        std::function<void(const std::any&)> callback;
    };

    mutable std::shared_mutex mtx_;
    std::unordered_map<std::type_index, std::vector<NamedSubscriber>> subscribers_;
    std::unordered_map<std::type_index, std::function<std::any(const std::any&)>> providers_;
};

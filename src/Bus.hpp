#pragma once

#include <algorithm>
#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class DataBus {
public:
    // Subscribe to every published event of exactly one Event type.
    template <typename Event>
    void subscribe(std::string_view identifier,std::function<void(const Event&)> callback) {
        subscribers_[typeid(Event)].push_back(
            NamedSubscriber{
                std::string{identifier},
                [callback = std::move(callback)](const std::any& data) {
                // Convert the erased event back to the exact subscribed Event type.
                callback(std::any_cast<const Event&>(data));
                }});
    }

    // Remove every named subscriber for one Event type.
    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        auto found = subscribers_.find(typeid(Event));
        if (found == subscribers_.end()) {
            return;
        }

        std::erase_if(found->second,[identifier](const NamedSubscriber& subscriber) {
                return subscriber.identifier == identifier;
            });
    }

    // Remove a provider for one Request type.
    template <typename Request>
    void unprovide() {providers_.erase(typeid(Request));}

    // Publish an event to all subscribers registered for this Event type.
    template <typename Event>
    void publish(const Event& event) const {
        auto found = subscribers_.find(typeid(Event));
        if (found == subscribers_.end()) {
            return;
        }

        // Execute each type-erased callback as a callback for Event.
        for (const auto& callback : found->second) {
            callback.callback(event);
        }
    }

    // Register the one provider that handles a Request type.
    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler) {
        providers_[typeid(Request)] =
            [handler = std::move(handler)](const std::any& request) -> std::any {
                // Convert the erased request and erase the returned response.
                return handler(std::any_cast<const Request&>(request));
            };
    }

    // Call the provider registered for the supplied Request type.
    template <typename Response, typename Request>
    Response call(const Request& request) const {
        auto found = providers_.find(typeid(Request));
        if (found == providers_.end()) {
            throw std::runtime_error("No component registered to handle this call");
        }

        // Convert the provider's erased response to the requested Response type.
        return std::any_cast<Response>(found->second(request));
    }

private:
    struct NamedSubscriber {
        std::string identifier;
        std::function<void(const std::any&)> callback;
    };

    // Event callbacks are erased after their Event type is used as the map key.
    std::unordered_map<std::type_index,std::vector<NamedSubscriber>> subscribers_;

    // Providers are keyed by request type and return type-erased responses.
    std::unordered_map<std::type_index,std::function<std::any(const std::any&)>> providers_;
};




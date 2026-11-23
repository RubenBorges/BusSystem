#pragma once

#include <iostream>
#include <fstream>
#include <print>
#include <format>
#include <chrono>

// Containers
#include <random>
#include <vector>
#include <array>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <functional>
#include <ranges>
#include <expected>
#include <optional>
#include <algorithm>
#include <utility>
#include <memory>
#include <tuple>
#include <type_traits>
#include <string>
#include <string_view>
#include <any>
#include <stdexcept>
#include <typeindex>
#include <eventType.hpp>
#include <Policies.hpp>
//--------------------------------

// Each MessageBus channel stores subscribers for exactly one message type.
template <typename MessageT,
          template <typename> class Container = VectorPolicy,
          typename SubscriptionPolicyT = NoopSubscriptionPolicy>
class MessageBus {
private:
    // A subscriber stores both its identifying string and callback.
    struct Subscriber_T {
        std::string identifier;
        std::function<void(const MessageT&)> callback;
    };

    // The storage policy owns the collection of subscribers for this message type.
    Container<Subscriber_T> subscribers;

    // The subscription policy owns lifecycle behavior such as logging or registration.
    SubscriptionPolicyT subscriptionPolicy;

public:
    static_assert(std::is_base_of_v<SubscriptionPolicy, SubscriptionPolicyT>,
                  "SubscriptionPolicyT must derive from SubscriptionPolicy");

    void subscribe(std::string_view identifier,
                   std::function<void(const MessageT&)> callback) {
        // Copy the identifier so it remains valid after the caller's string is destroyed.
        subscribers.add(Subscriber_T{std::string{identifier}, std::move(callback)});
        // Run lifecycle behavior with the exact subscriber identifier.
        subscriptionPolicy.subscribe(identifier);
    }

    void unsubscribe(std::string_view identifier) {
        // Remove only subscribers that use this exact identifying string.
        subscribers.remove_if([identifier](const Subscriber_T& subscriber) {
            return subscriber.identifier == identifier;
        });
        // Run lifecycle behavior with the exact subscriber identifier.
        subscriptionPolicy.unsubscribe(identifier);
    }

    void publish(const MessageT& message) {
        // Deliver the message to every subscriber stored in this channel.
        subscribers.for_each([&](const Subscriber_T& subscriber) {
            subscriber.callback(message);
        });
    }
};

// MultiMessageBus uses one MessageBus channel for each distinct message type.
template <typename... MessageTs>
class MultiMessageBus {
private:
    // A tuple avoids storing unrelated subscriber types in the same container.
    std::tuple<MessageBus<MessageTs, VectorPolicy, NoopSubscriptionPolicy>...> channels;

public:
    template <typename MessageT>
    MessageBus<MessageT, VectorPolicy, NoopSubscriptionPolicy>& channel() {
        // std::get finds the channel whose message type exactly matches MessageT.
        return std::get<MessageBus<MessageT, VectorPolicy, NoopSubscriptionPolicy>>(channels);
    }

    template <typename MessageT>
    void subscribe(std::string_view identifier,
                   std::function<void(const MessageT&)> subscriber) {
        // The selected channel owns named subscribers for this exact message type.
        channel<MessageT>().subscribe(identifier, std::move(subscriber));
    }

    template <typename MessageT>
    void unsubscribe(std::string_view identifier) {
        // Remove a named subscription from the channel for this exact message type.
        channel<MessageT>().unsubscribe(identifier);
    }

    template <typename MessageT>
    void publish(const MessageT& message) {
        // Publish only to subscribers registered for this exact message type.
        channel<MessageT>().publish(message);
    }
};


class DataBus {
public:
    // Subscribe to every published event of exactly one Event type.
    template <typename Event>
    void subscribe(std::string_view identifier,
                   std::function<void(const Event&)> callback) {
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

        std::erase_if(found->second,
            [identifier](const NamedSubscriber& subscriber) {
                return subscriber.identifier == identifier;
            });
    }

    // Remove a provider for one Request type.
    template <typename Request>
    void unprovide() {
        providers_.erase(typeid(Request));
    }

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
    std::unordered_map<
        std::type_index,
        std::vector<NamedSubscriber>> subscribers_;

    // Providers are keyed by request type and return type-erased responses.
    std::unordered_map<
        std::type_index,
        std::function<std::any(const std::any&)>> providers_;
};




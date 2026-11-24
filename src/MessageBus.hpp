// #pragma once

// #include <Policies.hpp>

// #include <functional>
// #include <string>
// #include <string_view>
// #include <type_traits>
// #include <utility>

// // Each MessageBus channel stores subscribers for exactly one message type.
// template <typename MessageT,
//           template <typename> class Container = VectorPolicy,
//           typename SubscriptionPolicyT = NoopSubscriptionPolicy>
// class MessageBus {
// private:
//     struct Subscriber_T {
//         std::string identifier;
//         std::function<void(const MessageT&)> callback;
//     };

//     Container<Subscriber_T> subscribers;
//     SubscriptionPolicyT subscriptionPolicy;

// public:
//     static_assert(std::is_base_of_v<SubscriptionPolicy, SubscriptionPolicyT>,
//                   "SubscriptionPolicyT must derive from SubscriptionPolicy");

//     void subscribe(std::string_view identifier,
//                    std::function<void(const MessageT&)> callback) {
//         subscribers.add(Subscriber_T{std::string{identifier}, std::move(callback)});
//         subscriptionPolicy.subscribe(identifier);
//     }

//     void unsubscribe(std::string_view identifier) {
//         subscribers.remove_if([identifier](const Subscriber_T& subscriber) {
//             return subscriber.identifier == identifier;
//         });
//         subscriptionPolicy.unsubscribe(identifier);
//     }

//     void publish(const MessageT& message) {
//         subscribers.for_each([&](const Subscriber_T& subscriber) {
//             subscriber.callback(message);
//         });
//     }
// };

module;
import std;
import MessageBus;
export module MultiMessageBus;

// // MultiMessageBus uses one MessageBus channel for each distinct message type.
export template <typename... MessageTs>
class MultiMessageBus {
private:
    std::tuple<MessageBus<MessageTs, VectorPolicy, NoopSubscriptionPolicy>...> channels;

public:
    template <typename MessageT>
    MessageBus<MessageT, VectorPolicy, NoopSubscriptionPolicy>& channel() {
        return std::get<MessageBus<MessageT, VectorPolicy, NoopSubscriptionPolicy>>(channels);
    }

    template <typename MessageT>
    void subscribe(std::string_view identifier,
                   std::function<void(const MessageT&)> subscriber) {
        channel<MessageT>().subscribe(identifier, std::move(subscriber));
    }

    template <typename MessageT>
    void unsubscribe(std::string_view identifier) {
        channel<MessageT>().unsubscribe(identifier);
    }

    template <typename MessageT>
    void publish(const MessageT& message) {
        channel<MessageT>().publish(message);
    }
};

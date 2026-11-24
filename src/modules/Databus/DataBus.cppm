export module DataBus;

import std;

// Central messaging hub: components publish events and subscribe to them, or
// provide/call request-response handlers. The bus knows nothing about storage,
// logging or threading policies; it only routes messages.
//
// Delivery guarantees:
//   * publish() never holds a lock while running callbacks, so callbacks may
//     freely publish, subscribe or unsubscribe.
//   * A throwing subscriber is isolated: the remaining subscribers still get the
//     event, and the publisher never sees the exception (see setErrorHandler).
//   * unsubscribe() blocks until a callback of that subscriber that is already
//     running on another thread has finished, and that callback is never invoked
//     afterwards. Components may therefore unsubscribe in stop()/the destructor
//     and then safely be destroyed. (Calling unsubscribe from inside the
//     subscriber's own callback is allowed.)
//   * Callbacks of ONE subscriber never run concurrently with each other.
export class DataBus {
public:
    using ErrorHandler =
        std::function<void(std::string_view subscriber, std::exception_ptr error)>;

    DataBus() = default;
    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    // --- Events -----------------------------------------------------------

    template <typename Event>
    void subscribe(std::string_view identifier, std::function<void(const Event&)> callback) {
        auto subscriber = std::make_shared<Subscriber>();
        subscriber->identifier = std::string{identifier};
        subscriber->callback =
            [callback = std::move(callback)](const std::any& data) {
                callback(std::any_cast<const Event&>(data));
            };

        std::unique_lock lock(mtx_);
        subscribers_[std::type_index{typeid(Event)}].push_back(std::move(subscriber));
    }

    // Removes every subscription of this Event type registered under `identifier`.
    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        std::vector<std::shared_ptr<Subscriber>> removed;
        {
            std::unique_lock lock(mtx_);
            auto found = subscribers_.find(std::type_index{typeid(Event)});
            if (found == subscribers_.end()) {
                return;
            }
            auto& list = found->second;
            for (auto it = list.begin(); it != list.end();) {
                if ((*it)->identifier == identifier) {
                    (*it)->active.store(false);
                    removed.push_back(std::move(*it));
                    it = list.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Wait (without holding the bus lock) for callbacks that are mid-flight.
        for (const auto& subscriber : removed) {
            std::lock_guard wait{subscriber->running};
        }
    }

    template <typename Event>
    void publish(const Event& event) const {
        std::vector<std::shared_ptr<Subscriber>> snapshot;
        {
            std::shared_lock lock(mtx_);
            auto found = subscribers_.find(std::type_index{typeid(Event)});
            if (found == subscribers_.end() || found->second.empty()) {
                return;
            }
            snapshot = found->second;   // copies shared_ptrs only
        }

        const std::any payload{event};  // the event is copied once, not once per subscriber
        for (const auto& subscriber : snapshot) {
            deliver(*subscriber, payload);
        }
    }

    template <typename Event>
    [[nodiscard]] std::size_t subscriberCount() const {
        std::shared_lock lock(mtx_);
        auto found = subscribers_.find(std::type_index{typeid(Event)});
        return found == subscribers_.end() ? 0 : found->second.size();
    }

    // --- Request / response -----------------------------------------------

    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler) {
        std::unique_lock lock(mtx_);
        providers_[std::type_index{typeid(Request)}] =
            [handler = std::move(handler)](const std::any& request) -> std::any {
                return handler(std::any_cast<const Request&>(request));
            };
    }

    template <typename Request>
    void unprovide() {
        std::unique_lock lock(mtx_);
        providers_.erase(std::type_index{typeid(Request)});
    }

    template <typename Request>
    [[nodiscard]] bool hasProvider() const {
        std::shared_lock lock(mtx_);
        return providers_.contains(std::type_index{typeid(Request)});
    }

    template <typename Response, typename Request>
    Response call(const Request& request) const {
        std::function<std::any(const std::any&)> handlerCopy;
        {
            std::shared_lock lock(mtx_);
            auto found = providers_.find(std::type_index{typeid(Request)});
            if (found == providers_.end()) {
                throw std::runtime_error("No component registered to handle this call");
            }
            handlerCopy = found->second;
        }

        return std::any_cast<Response>(handlerCopy(request));
    }

    // --- Diagnostics ------------------------------------------------------

    // Called (on the publishing thread) when a subscriber throws. The default
    // handler prints the subscriber's name and the exception message to stderr.
    void setErrorHandler(ErrorHandler handler) {
        std::unique_lock lock(mtx_);
        errorHandler_ = std::move(handler);
    }

private:
    struct Subscriber {
        std::string identifier;
        std::function<void(const std::any&)> callback;
        std::atomic<bool> active{true};
        std::recursive_mutex running;   // held while the callback executes
    };

    void deliver(Subscriber& subscriber, const std::any& payload) const {
        std::lock_guard guard(subscriber.running);
        if (!subscriber.active.load()) {
            return;
        }
        try {
            subscriber.callback(payload);
        } catch (...) {
            reportError(subscriber.identifier, std::current_exception());
        }
    }

    void reportError(const std::string& identifier, std::exception_ptr error) const {
        ErrorHandler handler;
        {
            std::shared_lock lock(mtx_);
            handler = errorHandler_;
        }
        try {
            if (handler) {
                handler(identifier, error);
                return;
            }
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& e) {
                std::cerr << "[DataBus] subscriber '" << identifier
                          << "' threw: " << e.what() << '\n';
            } catch (...) {
                std::cerr << "[DataBus] subscriber '" << identifier
                          << "' threw an unknown exception\n";
            }
        } catch (...) {
            // An error handler must not be able to break event delivery.
        }
    }

    mutable std::shared_mutex mtx_;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Subscriber>>> subscribers_;
    std::unordered_map<std::type_index, std::function<std::any(const std::any&)>> providers_;
    ErrorHandler errorHandler_;

};

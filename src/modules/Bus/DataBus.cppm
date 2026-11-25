/*
 *  DataBus: Dynamic Hub-and-Spoke (Runtime Type Erasure)
 *
 *  Central messaging hub: components publish events and subscribe to them, or
 *  provide/call request-response handlers. The bus knows nothing about storage,
 *  logging, or threading policies; it only routes messages.
 *
 *  Delivery & Safety Guarantees:
 *    * publish() and call() never hold the bus lock while running callbacks.
 *    * Throwing subscribers are isolated and routed to the error handler.
 *    * unsubscribe() and unprovide() block until any in-flight execution of
 *      that specific handler finishes, preventing use-after-free on teardown.
 *    * Callbacks of ONE subscriber never run concurrently with each other.
 *    * Recursion depth is bounded to prevent infinite event loops.
 */

export module DataBus;

import std;

export class DataBus {
private:
    struct Subscriber {
        std::string identifier;
        std::function<void(const void*)> callback;
        std::atomic<bool> active{true};
        std::recursive_mutex running;
    };

    struct Provider {
        std::type_index responseType;
        std::function<std::any(const std::any&)> handler;
        std::atomic<bool> active{true};
        std::recursive_mutex running;

        Provider(std::type_index respType, std::function<std::any(const std::any&)> fn)
            : responseType(respType), handler(std::move(fn)) {}
    };

    using SubscriberList = std::vector<std::shared_ptr<Subscriber>>;

public:
    using ErrorHandler =
        std::function<void(std::string_view subscriber, std::exception_ptr error)>;

    static constexpr std::size_t kMaxPublishDepth = 32;

    // --- RAII Subscription Guard ------------------------------------------
    class [[nodiscard]] ScopedSubscription {
    public:
        ScopedSubscription() = default;
        ScopedSubscription(std::function<void()> unsubscribeFn)
            : unsubscribeFn_(std::move(unsubscribeFn)) {}

        ~ScopedSubscription() { reset(); }

        ScopedSubscription(ScopedSubscription&& other) noexcept
            : unsubscribeFn_(std::exchange(other.unsubscribeFn_, nullptr)) {}

        ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
            if (this != &other) {
                reset();
                unsubscribeFn_ = std::exchange(other.unsubscribeFn_, nullptr);
            }
            return *this;
        }

        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        void reset() {
            if (auto fn = std::exchange(unsubscribeFn_, nullptr)) {
                fn();
            }
        }

    private:
        std::function<void()> unsubscribeFn_;
    };

    DataBus() = default;
    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    // --- Events -----------------------------------------------------------

    // Registers a callback and returns an RAII guard (can be ignored if managing manually).
    template <typename Event>
    ScopedSubscription subscribe(std::string_view identifier,
                                 std::function<void(const Event&)> callback) {
        auto subscriber = std::make_shared<Subscriber>();
        subscriber->identifier = identifier;
        subscriber->callback = [callback = std::move(callback)](const void* data) {
            callback(*static_cast<const Event*>(data));
        };

        const auto eventType = std::type_index{typeid(Event)};
        {
            std::unique_lock lock(mtx_);
            auto& slot = subscribers_[eventType];
            auto next = slot ? std::make_shared<SubscriberList>(*slot)
                             : std::make_shared<SubscriberList>();
            next->push_back(subscriber);
            slot = std::move(next);
        }

        return ScopedSubscription([this, eventType, weakSub = std::weak_ptr(subscriber)]() {
            unsubscribeInstance(eventType, weakSub);
        });
    }

    // Removes every subscription of `Event` registered under `identifier`.
    template <typename Event>
    void unsubscribe(std::string_view identifier) {
        SubscriberList removed;
        {
            std::unique_lock lock(mtx_);
            auto found = subscribers_.find(std::type_index{typeid(Event)});
            if (found == subscribers_.end() || !found->second) {
                return;
            }

            auto next = std::make_shared<SubscriberList>();
            next->reserve(found->second->size());

            for (const auto& sub : *found->second) {
                if (sub->identifier == identifier) {
                    sub->active.store(false);
                    removed.push_back(sub);
                } else {
                    next->push_back(sub);
                }
            }

            if (removed.empty()) {
                return;
            }
            found->second = std::move(next);
        }

        for (const auto& subscriber : removed) {
            std::lock_guard wait{subscriber->running};
        }
    }

    // Removes all subscriptions across ALL event types for `identifier`.
    void unsubscribeAll(std::string_view identifier) {
        SubscriberList removed;
        {
            std::unique_lock lock(mtx_);
            for (auto& [type, slot] : subscribers_) {
                if (!slot || slot->empty()) continue;

                bool modified = false;
                auto next = std::make_shared<SubscriberList>();
                next->reserve(slot->size());

                for (const auto& sub : *slot) {
                    if (sub->identifier == identifier) {
                        sub->active.store(false);
                        removed.push_back(sub);
                        modified = true;
                    } else {
                        next->push_back(sub);
                    }
                }

                if (modified) {
                    slot = std::move(next);
                }
            }
        }

        for (const auto& subscriber : removed) {
            std::lock_guard wait{subscriber->running};
        }
    }

    template <typename Event>
    void publish(const Event& event) const {
        if (publishDepth_ >= kMaxPublishDepth) {
            throw std::runtime_error("DataBus: maximum event recursion depth exceeded");
        }

        std::shared_ptr<const SubscriberList> snapshot;
        ErrorHandler errHandlerSnapshot;
        {
            std::shared_lock lock(mtx_);
            auto found = subscribers_.find(std::type_index{typeid(Event)});
            if (found == subscribers_.end() || !found->second || found->second->empty()) {
                return;
            }
            snapshot = found->second;
            errHandlerSnapshot = errorHandler_;
        }

        struct DepthGuard {
            DepthGuard() { ++publishDepth_; }
            ~DepthGuard() { --publishDepth_; }
        } depthGuard;

        for (const auto& subscriber : *snapshot) {
            deliver(*subscriber, &event, errHandlerSnapshot);
        }
    }

    template <typename Event>
    [[nodiscard]] std::size_t subscriberCount() const {
        std::shared_lock lock(mtx_);
        auto found = subscribers_.find(std::type_index{typeid(Event)});
        return (found == subscribers_.end() || !found->second) ? 0 : found->second->size();
    }

    // --- Request / Response -----------------------------------------------

    template <typename Request, typename Response>
    void provide(std::function<Response(const Request&)> handler, bool overwrite = true) {
        auto provider = std::make_shared<Provider>(
            std::type_index{typeid(Response)},
            [handler = std::move(handler)](const std::any& request) -> std::any {
                return handler(std::any_cast<const Request&>(request));
            }
        );

        std::shared_ptr<Provider> previous;
        {
            std::unique_lock lock(mtx_);
            const auto reqType = std::type_index{typeid(Request)};
            if (!overwrite && providers_.contains(reqType)) {
                throw std::runtime_error("DataBus: provider already registered for request type");
            }
            if (auto it = providers_.find(reqType); it != providers_.end()) {
                previous = std::move(it->second);
                previous->active.store(false);
            }
            providers_[reqType] = std::move(provider);
        }

        // Wait for any in-flight call on the replaced provider to finish
        if (previous) {
            std::lock_guard wait{previous->running};
        }
    }

    template <typename Request>
    void unprovide() {
        std::shared_ptr<Provider> removed;
        {
            std::unique_lock lock(mtx_);
            auto it = providers_.find(std::type_index{typeid(Request)});
            if (it == providers_.end()) {
                return;
            }
            removed = std::move(it->second);
            removed->active.store(false);
            providers_.erase(it);
        }

        // Wait (without holding bus lock) for any active call() to finish
        std::lock_guard wait{removed->running};
    }

    template <typename Request>
    [[nodiscard]] bool hasProvider() const {
        std::shared_lock lock(mtx_);
        return providers_.contains(std::type_index{typeid(Request)});
    }

        template <typename Response, typename Request>
    Response call(const Request& request) const {
        std::shared_ptr<Provider> provider;
        {
            std::shared_lock lock(mtx_);
            auto found = providers_.find(std::type_index{typeid(Request)});
            if (found == providers_.end() || !found->second) {
                throw std::runtime_error("DataBus: no component registered to handle this call");
            }
            provider = found->second;
        }

        if (provider->responseType != std::type_index{typeid(Response)}) {
            throw std::logic_error("DataBus: requested Response type does not match provider");
        }

        std::lock_guard guard(provider->running);
        if (!provider->active.load()) {
            throw std::runtime_error("DataBus: provider was unregistered before call executed");
        }

        return std::any_cast<Response>(provider->handler(request));
    }

    // --- Diagnostics ------------------------------------------------------

    void setErrorHandler(ErrorHandler handler) {
        std::unique_lock lock(mtx_);
        errorHandler_ = std::move(handler);
    }

private:
    void unsubscribeInstance(std::type_index eventType, std::weak_ptr<Subscriber> weakSub) {
        auto target = weakSub.lock();
        if (!target) return;

        target->active.store(false);
        {
            std::unique_lock lock(mtx_);
            auto found = subscribers_.find(eventType);
            if (found != subscribers_.end() && found->second) {
                auto next = std::make_shared<SubscriberList>();
                next->reserve(found->second->size());
                for (const auto& sub : *found->second) {
                    if (sub != target) {
                        next->push_back(sub);
                    }
                }
                found->second = std::move(next);
            }
        }

        std::lock_guard wait{target->running};
    }

    static void deliver(Subscriber& subscriber,
                        const void* payload,
                        const ErrorHandler& errorHandler) {
        std::lock_guard guard(subscriber.running);
        if (!subscriber.active.load()) {
            return;
        }
        try {
            subscriber.callback(payload);
        } catch (...) {
            reportError(subscriber.identifier, std::current_exception(), errorHandler);
        }
    }

    static void reportError(std::string_view identifier,
                            std::exception_ptr error,
                            const ErrorHandler& errorHandler) {
        if (errorHandler) {
            try {
                errorHandler(identifier, error);
                return;
            } catch (...) {
                // Fall through to default stderr logging if custom handler throws
            }
        }

        try {
            if (error) std::rethrow_exception(error);
        } catch (const std::exception& e) {
            std::println(stderr, "[DataBus] subscriber '{}' threw: {}", identifier, e.what());
        } catch (...) {
            std::println(stderr, "[DataBus] subscriber '{}' threw an unknown exception", identifier);
        }
    }

    static inline thread_local std::size_t publishDepth_ = 0;

    mutable std::shared_mutex mtx_;
    std::unordered_map<std::type_index, std::shared_ptr<const SubscriberList>> subscribers_;
    std::unordered_map<std::type_index, std::shared_ptr<Provider>> providers_;
    ErrorHandler errorHandler_;
};

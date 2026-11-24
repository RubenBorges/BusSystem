# 0 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Components/NotifyingComponent.cppm"
# 1 "/home/boopy/dev/Projects/DesignedSystem/build/linux-dev//"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "/home/boopy/dev/Projects/DesignedSystem/src/Modules/Components/NotifyingComponent.cppm"
export module NotifyingComponent;

import std;
import Components;
import EventType;

export class NotifyingComponent final : public Component {
public:
    explicit NotifyingComponent(DataBus& bus) : bus_(bus) {}

    void start() override {
        if (running_) {
            return;
        }

        bus_.subscribe<NotifyEvent>(SubscriptionId,[this](const NotifyEvent& event) { print(event); });
        running_ = true;
    }

    ~NotifyingComponent() override {
        if (running_) bus_.unsubscribe<NotifyEvent>(SubscriptionId);
    }

    void stop() override {
        if (!running_) return;

        bus_.unsubscribe<NotifyEvent>(SubscriptionId);
        running_ = false;
    }

private:
    static constexpr std::string_view SubscriptionId{"notifying-component"};

    static constexpr std::string_view typeName(NotifyEvent::Type type) {
        switch (type) {
        case NotifyEvent::Type::None: return "NONE";
        case NotifyEvent::Type::Note: return "NOTE";
        case NotifyEvent::Type::Warning: return "WARNING";
        case NotifyEvent::Type::Error: return "ERROR";
        case NotifyEvent::Type::Critical: return "CRITICAL";
        case NotifyEvent::Type::Sys: return "SYSTEM";
        }
        return "UNKNOWN";
    }

    void print(const NotifyEvent& event) {
        std::lock_guard lock(outputMutex_);
        std::ostream& output =
            event.type == NotifyEvent::Type::Warning ||
                    event.type == NotifyEvent::Type::Error ||
                    event.type == NotifyEvent::Type::Critical
                ? std::cerr
                : std::cout;
        output << std::format(
            "[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
            event.now,
            typeName(event.type),
            event.message) << std::flush;
        if (!output) {
            throw std::runtime_error("Could not print notification to the terminal");
        }
    }

    DataBus& bus_;
    std::mutex outputMutex_;
    bool running_{false};
};

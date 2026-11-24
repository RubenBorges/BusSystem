export module LoggingComponent;

import std;
import Components;
import EventType;

export class LoggingComponent final : public Component<> {
public:
    explicit LoggingComponent(DataBus& bus, std::filesystem::path logFilePath)
        : Component<>(bus), logFilePath_(std::move(logFilePath)) {}

    ~LoggingComponent() override {
        if (running_) {
            this->bus_.template unsubscribe<LogEntry>(SubscriptionId);
        }
    }

    void start() override {
        if (running_) {
            return;
        }

        if (!logFile_.is_open()) {
            logFile_.open(logFilePath_, std::ios::out | std::ios::app);
            if (!logFile_) {
                throw std::runtime_error(
                    std::format("Could not open log file '{}'", logFilePath_.string()));
            }
        }

        this->bus_.template subscribe<LogEntry>(
            SubscriptionId,
            [this](const LogEntry& entry) { write(entry); });
        running_ = true;
    }

    void stop() override {
        if (!running_) {
            return;
        }

        this->bus_.template unsubscribe<LogEntry>(SubscriptionId);
        running_ = false;

        std::lock_guard lock(writeMutex_);
        logFile_.flush();
        if (!logFile_) {
            throw std::runtime_error(
                std::format("Could not flush log file '{}'", logFilePath_.string()));
        }
        logFile_.close();
        if (logFile_.fail()) {
            throw std::runtime_error(
                std::format("Could not close log file '{}'", logFilePath_.string()));
        }
    }

private:
    static constexpr std::string_view SubscriptionId{"logging-component"};

    static constexpr std::string_view typeName(LogEntry::Type type) {
        switch (type) {
        case LogEntry::Type::None: return "NONE";
        case LogEntry::Type::Note: return "NOTE";
        case LogEntry::Type::Warning: return "WARNING";
        case LogEntry::Type::Error: return "ERROR";
        case LogEntry::Type::Critical: return "CRITICAL";
        case LogEntry::Type::Sys: return "SYSTEM";
        }
        return "UNKNOWN";
    }

    void write(const LogEntry& entry) {
        std::lock_guard lock(writeMutex_);
        logFile_ << std::format(
            "[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
            entry.now,
            typeName(entry.type),
            entry.message);
        if (!logFile_) {
            throw std::runtime_error(
                std::format("Could not write to log file '{}'", logFilePath_.string()));
        }
    }

    std::filesystem::path logFilePath_;
    std::ofstream logFile_;
    std::mutex writeMutex_;
    bool running_{false};
};
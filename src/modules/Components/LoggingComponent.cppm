export module LoggingComponent;

import std;
import Components;
import EventType;

export class LoggingComponent final : public Component<> {
public:
    explicit LoggingComponent(DataBus& bus, std::filesystem::path logFilePath)
        : Component<>(bus, "logging-component"), logFilePath_(std::move(logFilePath)) {}

    ~LoggingComponent() override {
        if (started_) bus_.unsubscribe<LogEntry>(name());
    }

    void start() override {
        if (started_) return;

        if (!file_) {
            file_.reset(std::fopen(logFilePath_.string().c_str(), "ab"));
            if (!file_) {
                throw std::runtime_error(
                    std::format("Could not open log file '{}'", logFilePath_.string()));
            }
        }

        bus_.subscribe<LogEntry>(name(), [this](const LogEntry& entry) { write(entry); });
        started_ = true;
    }

    void stop() override {
        if (!started_) return;

        // Blocks until any in-flight write() callback finishes, guaranteeing exclusive file_ access below.
        bus_.unsubscribe<LogEntry>(name());
        started_ = false;

        if (file_) {
            if (std::fflush(file_.get()) != 0) {
                throw std::runtime_error(
                    std::format("Could not flush log file '{}'", logFilePath_.string()));
            }
            if (std::fclose(file_.release()) != 0) {
                throw std::runtime_error(
                    std::format("Could not close log file '{}'", logFilePath_.string()));
            }
        }
    }

private:
    using FilePtr = std::unique_ptr<std::FILE, decltype([](std::FILE* f) noexcept {
        if (f) std::fclose(f);
    })>;

    static constexpr std::string_view typeName(LogEntry::Type type) noexcept {
        constexpr std::array<std::pair<LogEntry::Type, std::string_view>, 6> names{{
            {LogEntry::Type::None,     "NONE"},
            {LogEntry::Type::Note,     "NOTE"},
            {LogEntry::Type::Warning,  "WARNING"},
            {LogEntry::Type::Error,    "ERROR"},
            {LogEntry::Type::Critical, "CRITICAL"},
            {LogEntry::Type::Sys,      "SYSTEM"},
        }};

        const auto it = std::ranges::find(names, type, &std::pair<LogEntry::Type, std::string_view>::first);
        return it != names.end() ? it->second : "UNKNOWN";
    }

    void write(const LogEntry& entry) {
        std::print(file_.get(),
            "[{:%Y-%m-%d %H:%M:%S}] [{}] [{}] {}\n",
            entry.now,
            typeName(entry.type),
            entry.source,
            entry.message);

        if (std::ferror(file_.get()) != 0) {
            throw std::runtime_error(
                std::format("Could not write to log file '{}'", logFilePath_.string()));
        }
    }

    std::filesystem::path logFilePath_;
    FilePtr file_{nullptr};
};

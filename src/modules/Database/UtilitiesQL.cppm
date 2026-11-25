module;

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

export module UtilitiesQL;

import std;
export import EventType;
export import Pqxx;
export import DatabaseCredentials;

namespace bpql {

export inline bool table_exists(pqxx::transaction_base& txn,
                                std::string_view table_name) {
    pqxx::params p;
    p.append(table_name);

    const pqxx::result r = txn.exec("SELECT to_regclass($1) IS NOT NULL", p);
    return r.one_field().as<bool>();
}

export [[nodiscard]] inline std::expected<DatabaseCredentials, std::string>
loadCredentials(const std::filesystem::path& fpath) {
    const int rawFd = ::open(fpath.c_str(), O_RDONLY | O_CLOEXEC);
    if (rawFd < 0) {
        return std::unexpected(
            std::format("Failed to open credentials file '{}': {}",
                        fpath.string(), std::strerror(errno)));
    }

    // RAII guard for the POSIX file descriptor
    auto closeFd = [](int* fd) noexcept {
        if (fd && *fd >= 0) {
            ::close(*fd);
        }
    };
    int fdStorage = rawFd;
    std::unique_ptr<int, decltype(closeFd)> fdGuard(&fdStorage, closeFd);

    struct stat st{};
    if (::fstat(rawFd, &st) < 0) {
        return std::unexpected(
            std::format("Failed to stat credentials file '{}': {}",
                        fpath.string(), std::strerror(errno)));
    }

    constexpr std::size_t MaxCredFileSize = 64 * 1024;
    if (st.st_size <= 0 || static_cast<std::size_t>(st.st_size) > MaxCredFileSize) {
        return std::unexpected("Credentials file is empty or exceeds 64 KiB limit");
    }

    std::string buffer(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t totalRead = 0;
    while (totalRead < buffer.size()) {
        const ssize_t n = ::read(rawFd, buffer.data() + totalRead, buffer.size() - totalRead);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(
                std::format("Error reading credentials file '{}': {}",
                            fpath.string(), std::strerror(errno)));
        }
        if (n == 0) {
            break;
        }
        totalRead += static_cast<std::size_t>(n);
    }
    buffer.resize(totalRead);

    // Zero-copy line splitting with C++23 std::views::split + std::string_view
    std::array<std::string_view, 5> lines{};
    std::size_t lineIdx = 0;

    for (auto&& lineRange : std::views::split(std::string_view{buffer}, '\n')) {
        if (lineIdx >= lines.size()) {
            break;
        }
        std::string_view line{lineRange};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1); // Handle CRLF files cleanly
        }
        lines[lineIdx++] = line;
    }

    if (lineIdx < lines.size()) {
        return std::unexpected("Credentials file is incomplete (expected 5 lines)");
    }

    return DatabaseCredentials{
        .dbname   = std::string{lines[0]},
        .user     = std::string{lines[1]},
        .password = std::string{lines[2]},
        .hostaddr = std::string{lines[3]},
        .port     = std::string{lines[4]}
    };
}

export [[nodiscard]] constexpr std::expected<DatabaseCredentials, std::string>
validate(DatabaseCredentials c) {
    if (c.dbname.empty() || c.user.empty()) {
        return std::unexpected("Database name and user cannot be empty");
    }
    return c;
}

export [[nodiscard]] inline std::string toConnString(const DatabaseCredentials& c) {
    return c.connection_string();
}

export constexpr bool isIdentifierStart(char value) noexcept {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           value == '_';
}

export constexpr bool isIdentifierPart(char value) noexcept {
    return isIdentifierStart(value) ||
           (value >= '0' && value <= '9') ||
           value == '$';
}

export constexpr bool isSqlIdentifier(std::string_view value, bool allowSchema) noexcept {
    bool segmentStart = true;
    for (char character : value) {
        if (allowSchema && character == '.') {
            if (segmentStart) {
                return false;
            }
            segmentStart = true;
        } else if (segmentStart ? isIdentifierStart(character)
                                : isIdentifierPart(character)) {
            segmentStart = false;
        } else {
            return false;
        }
    }
    return !segmentStart;
}

export constexpr std::string_view statusName(DatabaseTx::Status status) noexcept {
    switch (status) {
    case DatabaseTx::Status::Waiting: return "waiting";
    case DatabaseTx::Status::Fail:    return "failed";
    case DatabaseTx::Status::Success: return "succeeded";
    }
    return "unknown";
}

} // namespace bpql

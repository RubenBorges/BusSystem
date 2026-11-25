module;

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/asio/ssl/context.hpp>
#include <boost/redis/config.hpp>

export module UtilitiesVK;

export import EventType;
export import DatabaseCredentials;
import std;


export namespace rdvk {

namespace detail {

[[nodiscard]] inline std::expected<std::string, std::string>
readSecureFile(const std::filesystem::path& fpath) {
    const int rawFd = ::open(fpath.c_str(), O_RDONLY | O_CLOEXEC);
    if (rawFd < 0) {
        return std::unexpected(
            std::format("Failed to open credentials file '{}': {}",
                        fpath.string(), std::strerror(errno)));
    }

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
    return buffer;
}

template <std::size_t N>
[[nodiscard]] inline std::expected<std::array<std::string_view, N>, std::string>
splitLines(std::string_view buffer) {
    std::array<std::string_view, N> lines{};
    std::size_t lineIdx = 0;

    for (auto&& lineRange : std::views::split(buffer, '\n')) {
        if (lineIdx >= lines.size()) {
            break;
        }
        std::string_view line{lineRange};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines[lineIdx++] = line;
    }

    if (lineIdx < lines.size()) {
        return std::unexpected(
            std::format("Credentials file is incomplete (expected {} lines, got {})", N, lineIdx));
    }
    return lines;
}

[[nodiscard]] inline std::expected<int, std::string> parseDbIndex(std::string_view sv) noexcept {
    if (sv.empty()) {
        return 0;
    }
    int value = 0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec != std::errc{} || ptr != sv.data() + sv.size() || value < 0) {
        return std::unexpected(std::format("Invalid Valkey database index: '{}'", sv));
    }
    return value;
}

} // namespace detail

// Expected 5-line file format:
//   Line 1: host
//   Line 2: port
//   Line 3: username
//   Line 4: password
//   Line 5: db_index
 [[nodiscard]] inline std::expected<credentials::ValkeyCredentials, std::string>
loadValkeyCredentials(const std::filesystem::path& fpath) {
    auto buffer = detail::readSecureFile(fpath);
    if (!buffer) {
        return std::unexpected(std::move(buffer.error()));
    }

    auto lines = detail::splitLines<5>(*buffer);
    if (!lines) {
        return std::unexpected(std::move(lines.error()));
    }

    auto dbIndex = detail::parseDbIndex((*lines)[4]);
    if (!dbIndex) {
        return std::unexpected(std::move(dbIndex.error()));
    }

    return credentials::ValkeyCredentials{
        .host     = std::string{(*lines)[0]},
        .port     = std::string{(*lines)[1]},
        .username = std::string{(*lines)[2]},
        .password = std::string{(*lines)[3]},
        .db_index = *dbIndex
    };
}

// Expected 8-line file format:
//   Lines 1-5: host, port, username, password, db_index
//   Line 6: ca_cert_path
//   Line 7: client_cert_path
//   Line 8: client_key_path
 [[nodiscard]] inline std::expected<credentials::ValkeySslCredentials, std::string>
loadValkeySslCredentials(const std::filesystem::path& fpath) {
    auto buffer = detail::readSecureFile(fpath);
    if (!buffer) {
        return std::unexpected(std::move(buffer.error()));
    }

    auto lines = detail::splitLines<8>(*buffer);
    if (!lines) {
        return std::unexpected(std::move(lines.error()));
    }

    auto dbIndex = detail::parseDbIndex((*lines)[4]);
    if (!dbIndex) {
        return std::unexpected(std::move(dbIndex.error()));
    }

    return credentials::ValkeySslCredentials{
        .host             = std::string{(*lines)[0]},
        .port             = std::string{(*lines)[1]},
        .username         = std::string{(*lines)[2]},
        .password         = std::string{(*lines)[3]},
        .db_index         = *dbIndex,
        .ca_cert_path     = std::string{(*lines)[5]},
        .client_cert_path = std::string{(*lines)[6]},
        .client_key_path  = std::string{(*lines)[7]}
    };
}

 [[nodiscard]] constexpr std::expected<credentials::ValkeyCredentials, std::string>
validate(credentials::ValkeyCredentials c) {
    if (c.host.empty() || c.port.empty()) {
        return std::unexpected("Valkey host and port cannot be empty");
    }
    if (c.db_index < 0) {
        return std::unexpected("Valkey database index cannot be negative");
    }
    return c;
}

 [[nodiscard]] constexpr std::expected<credentials::ValkeySslCredentials, std::string>
validate(credentials::ValkeySslCredentials c) {
    if (c.host.empty() || c.port.empty()) {
        return std::unexpected("Valkey host and port cannot be empty");
    }
    if (c.db_index < 0) {
        return std::unexpected("Valkey database index cannot be negative");
    }
    if (c.ca_cert_path.empty()) {
        return std::unexpected("CA certificate path cannot be empty for SSL credentials");
    }
    if (c.client_cert_path.empty() != c.client_key_path.empty()) {
        return std::unexpected("Both client certificate and client key paths must be provided together");
    }
    return c;
}

 [[nodiscard]] inline std::string toConnString(const credentials::ValkeyCredentials& c) {
    return c.connection_string();
}

 [[nodiscard]] inline std::string toConnString(const credentials::ValkeySslCredentials& c) {
    return c.connection_string();
}

// Boost 1.92 boost::redis::config builders
 [[nodiscard]] inline boost::redis::config
toBoostConfig(const credentials::ValkeyCredentials& c) {
    boost::redis::config cfg;
    cfg.addr.host = c.host;
    cfg.addr.port = c.port;
    cfg.use_ssl = false;

    if (!c.password.empty() || c.db_index != 0) {
        cfg.use_setup = true;
        cfg.setup.clear();

        if (!c.password.empty()) {
            cfg.setup.hello(c.username.empty() ? "default" : c.username, c.password);
        } else {
            cfg.setup.hello();
        }

        if (c.db_index != 0) {
            cfg.setup.push("SELECT", c.db_index);
        }
    }
    return cfg;
}

 [[nodiscard]] inline boost::redis::config
toBoostConfig(const credentials::ValkeySslCredentials& c) {
    boost::redis::config cfg;
    cfg.addr.host = c.host;
    cfg.addr.port = c.port;
    cfg.use_ssl = true;

    if (!c.password.empty() || c.db_index != 0) {
        cfg.use_setup = true;
        cfg.setup.clear();

        if (!c.password.empty()) {
            cfg.setup.hello(c.username.empty() ? "default" : c.username, c.password);
        } else {
            cfg.setup.hello();
        }

        if (c.db_index != 0) {
            cfg.setup.push("SELECT", c.db_index);
        }
    }
    return cfg;
}

// Boost 1.92 OpenSSL context builder for ValkeySslCredentials
 [[nodiscard]] inline std::expected<boost::asio::ssl::context, std::string>
toSslContext(const credentials::ValkeySslCredentials& c) {
    namespace ssl = boost::asio::ssl;
    boost::system::error_code ec;

    ssl::context ctx{ssl::context::tlsv13_client};
    ctx.set_verify_mode(ssl::verify_peer, ec);
    if (ec) {
        return std::unexpected(std::format("Failed to set SSL verify mode: {}", ec.message()));
    }

    if (!c.ca_cert_path.empty()) {
        ctx.load_verify_file(c.ca_cert_path, ec);
        if (ec) {
            return std::unexpected(std::format("Failed to load CA cert '{}': {}", c.ca_cert_path, ec.message()));
        }
    } else {
        ctx.set_default_verify_paths(ec);
        if (ec) {
            return std::unexpected(std::format("Failed to load default verify paths: {}", ec.message()));
        }
    }

    if (!c.client_cert_path.empty()) {
        ctx.use_certificate_chain_file(c.client_cert_path, ec);
        if (ec) {
            return std::unexpected(std::format("Failed to load client cert '{}': {}", c.client_cert_path, ec.message()));
        }
    }

    if (!c.client_key_path.empty()) {
        ctx.use_private_key_file(c.client_key_path, ssl::context::pem, ec);
        if (ec) {
            return std::unexpected(std::format("Failed to load client key '{}': {}", c.client_key_path, ec.message()));
        }
    }

    return ctx;
}

// Key validation helper (mirrors isSqlIdentifier in UtilitiesQL)
 constexpr bool isValkeyKey(std::string_view key) noexcept {
    if (key.empty() || key.size() > 512 * 1024 * 1024) {
        return false;
    }
    for (unsigned char ch : key) {
        if (ch < 0x20 || ch == 0x7F) { // Reject ASCII control characters / newlines
            return false;
        }
    }
    return true;
}

} // namespace bpvk

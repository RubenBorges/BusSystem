#pragma once

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <pqxx/pqxx>

// Key<->Value: pqxx::connection conn("host=127.0.0.1 port=5432 dbname=mydb user=postgres password=secret");
// URI: pqxx::connection conn("postgresql://postgres:secret@localhost:5432/mydb");

struct DatabaseCredentials {
    std::string dbname;
    std::string user;
    std::string password;
    std::string hostaddr;
    std::string port;

    [[nodiscard]] std::string connection_string() const {
        return std::format(
            "dbname={} user={} password={} hostaddr={} port={}",
            dbname, user, password, hostaddr, port
        );
    }
};

namespace bpql {

inline bool table_exists(pqxx::transaction_base& txn, const std::string& table_name) {
    pqxx::params p;
    p.append(table_name);

    pqxx::result r = txn.exec("SELECT to_regclass($1) IS NOT NULL", p);
    return r.one_field().as<bool>();
}

[[nodiscard]] inline std::expected<DatabaseCredentials, std::string> loadCredentials(const std::filesystem::path& fpath) {
    std::ifstream ifile(fpath);
    if (!ifile) {
        return std::unexpected(std::format("Failed to open credentials file: {}", fpath.string()));
    }

    DatabaseCredentials db;
    if (!std::getline(ifile, db.dbname) ||
        !std::getline(ifile, db.user) ||
        !std::getline(ifile, db.password) ||
        !std::getline(ifile, db.hostaddr) ||
        !std::getline(ifile, db.port)) {
        return std::unexpected("Credentials file is incomplete (expected 5 lines)");
    }

    return db;
}

[[nodiscard]] inline std::expected<DatabaseCredentials, std::string> validate(DatabaseCredentials c) {
    if (c.dbname.empty() || c.user.empty()) {
        return std::unexpected("Database name and user cannot be empty");
    }
    return c;
}

[[nodiscard]] inline std::string toConnString(const DatabaseCredentials& c) {
    return c.connection_string();
}

} // namespace bpql

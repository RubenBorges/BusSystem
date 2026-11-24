#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <pqxx/pqxx>

#include <Components.hpp>
#include <UtilitiesQL.hpp>

#ifndef MAXCOLUMNS__
    #define MAXCOLUMNS__ 64
#endif

inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

enum class txnState : unsigned char { invalid = '0', valid = '1' };

class DatabaseComponent : public Component {
    DataBus& bus_;
    bool started_ = false;
    std::string currentTransaction_;

    // Lightweight fixed-capacity views for column names (zero heap allocations)
    std::array<std::string_view, MaxCols> transactionArgs_{};
    std::size_t argCount_ = 0;

    // Build pqxx::params directly instead of buffering in std::vector<std::string>
    pqxx::params transactionParams_;
    std::size_t valCount_ = 0;

    std::string tableName;
    std::string connStr_;
    std::optional<pqxx::connection> conn_;

public:
    txnState transactionState{txnState::invalid};

    explicit DatabaseComponent(DataBus& bus,std::string connectionString = "dbname=postgres user=postgres hostaddr=localhost port=5432")
        : bus_(bus), connStr_(std::move(connectionString)) {currentTransaction_.reserve(512);}

    explicit DatabaseComponent(DataBus& bus, const std::filesystem::path& configFile)
        : DatabaseComponent(bus, resolveConnectionString(configFile)) {}

    void start() override {
        if (!conn_ || !conn_->is_open()) {
            conn_.emplace(connStr_);
        }
        started_ = true;
    }

    void stop() override {
        conn_.reset();
        started_ = false;
    }

    // --- Lightweight Builders ---

    void setTableName(std::string_view name) { tableName = name; }

    void addArgs(std::initializer_list<std::string_view> argList) {
        for (std::string_view arg : argList) addArg(arg);
    }

    template <typename... Ts>
    void addVals(Ts&&... vals) {
        (addVal(std::forward<Ts>(vals)), ...);
    }

    void resetTransactionState() {
        argCount_ = 0;
        valCount_ = 0;
        transactionParams_ = pqxx::params{};
        currentTransaction_.clear();
        transactionState = txnState::invalid;
    }

    void buildTransactionFromArgsandVals() {
        transactionState = txnState::invalid;
        currentTransaction_.clear();

        if (tableName.empty()) {
            std::println(stderr, "Table name must be set before building a transaction.");
            return;
        }

        if (argCount_ == 0 || argCount_ != valCount_) {
            std::println(stderr, "Argument and value counts must be non-empty and equal.");
            return;
        }

        currentTransaction_ += "INSERT INTO ";
        currentTransaction_ += tableName;
        currentTransaction_ += " (";

        for (std::size_t i = 0; i < argCount_; ++i) {
            currentTransaction_ += transactionArgs_[i];
            currentTransaction_ += (i + 1 < argCount_) ? ", " : ") VALUES (";
        }

        for (std::size_t i = 0; i < valCount_; ++i) {
            std::format_to(std::back_inserter(currentTransaction_), "${}", i + 1);
            currentTransaction_ += (i + 1 < valCount_) ? ", " : ")";
        }

        transactionState = txnState::valid;
    }

    // --- Execution ---

    int processTransaction() {
        return (transactionState == txnState::valid) ? executeParams(transactionParams_) : 1;
    }

    template <typename... Args>
    int writeToDB(Args&&... args) {
        pqxx::params params{std::forward<Args>(args)...};
        return executeParams(params);
    }

private:
    static std::string resolveConnectionString(const std::filesystem::path& configFile) {
        return bpql::loadCredentials(configFile)
            .and_then(bpql::validate)
            .transform(bpql::toConnString)
            .or_else([](const std::string& err) -> std::expected<std::string, std::string> {
                std::println(stderr, "Primary config failed ({}), trying fallback...", err);
                return bpql::loadCredentials("fallback_creds.txt")
                    .and_then(bpql::validate)
                    .transform(bpql::toConnString);
            })
            .value_or("dbname=postgres user=postgres hostaddr=localhost port=5432");
    }

    void addArg(std::string_view arg) {
        if (argCount_ >= MaxCols) {
            throw std::out_of_range("Exceeded MaxCols limit");
        }
        transactionArgs_[argCount_++] = arg;
    }

    template <typename T>
    void addVal(T&& val) {
        transactionParams_.append(std::forward<T>(val));
        ++valCount_;
    }

    int executeParams(const pqxx::params& params) {
        if (!conn_ || !conn_->is_open()) {
            std::println(stderr, "Database connection is not open.");
            return 1;
        }

        if (currentTransaction_.empty()) {
            std::println(stderr, "No transaction SQL scaffolding set.");
            return 1;
        }

        try {
            pqxx::work txn(*conn_);
            txn.exec(currentTransaction_, params);
            txn.commit();
            resetTransactionState();
            return 0;
        } catch (const std::exception& e) {
            std::println(stderr, "Database error: {}", e.what());
            return 1;
        }
    }
};

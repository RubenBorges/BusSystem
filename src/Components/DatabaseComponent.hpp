#pragma once
#include <pqxx/pqxx>

import std;
import Components;
import UtilitiesQL;


#ifndef MAXCOLUMNS__
#define MAXCOLUMNS__ 64
#endif
import Components;

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

    explicit DatabaseComponent(DataBus& bus, std::string connectionString = "dbname=postgres user=postgres hostaddr=localhost port=5432")
        : bus_(bus), connStr_(std::move(connectionString)) {
        currentTransaction_.reserve(512);
    }

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
        if (transactionState != txnState::valid) {
            publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
            return 1;
        }
        return executeParams(transactionParams_, DatabaseTx::Mode::Write);
    }

    template <typename... Args>
    int writeToDB(Args&&... args) {
        pqxx::params params{std::forward<Args>(args)...};
        return executeParams(params, DatabaseTx::Mode::Write);
    }

    template <typename... Args>
    int readFromDB(Args&&... args) {
        pqxx::params params{std::forward<Args>(args)...};
        return executeParams(params, DatabaseTx::Mode::Read);
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

    void publishTxStatus(DatabaseTx::Mode mode,
                         DatabaseTx::Status status,
                         std::uint64_t pgXid) {
        bus_.publish(DatabaseTx{
            .mode = mode,
            .status = status,
            .pgXid = pgXid
        });
    }

    int executeParams(const pqxx::params& params, DatabaseTx::Mode mode) {
        if (!conn_ || !conn_->is_open()) {
            std::println(stderr, "Database connection is not open.");
            publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
            return 1;
        }

        if (currentTransaction_.empty()) {
            std::println(stderr, "No transaction SQL scaffolding set.");
            publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
            return 1;
        }

        std::uint64_t pgXid = 0;

        try {
            if (mode == DatabaseTx::Mode::Write) {
                pqxx::work txn(*conn_);

                // Allocate and retrieve the real 64-bit Postgres transaction ID (xid8)
                // Note: Use "SELECT txid_current()" if running PostgreSQL 12 or older
                pgXid = txn.query_value<std::uint64_t>("SELECT pg_current_xact_id()::text::bigint");

                publishTxStatus(mode, DatabaseTx::Status::Waiting, pgXid);

                txn.exec(currentTransaction_, params);
                txn.commit();
            } else {
                // Read-only transactions use virtual XIDs (pgXid stays 0 to avoid XID burn)
                publishTxStatus(mode, DatabaseTx::Status::Waiting, 0);

                pqxx::read_transaction txn(*conn_);
                txn.exec(currentTransaction_, params);
                txn.commit();
            }

            resetTransactionState();
            publishTxStatus(mode, DatabaseTx::Status::Success, pgXid);
            return 0;
        } catch (const std::exception& e) {
            std::println(stderr, "Database error (XID {}): {}", pgXid, e.what());
            publishTxStatus(mode, DatabaseTx::Status::Fail, pgXid);
            return 1;
        }
    }
};

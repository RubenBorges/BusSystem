// module;
// #include <cstdio>

// export module DatabaseComponent;

// import std;
// import TaskPolicies;
// export import Pqxx;          // pqxx::result is part of the public API now
// export import Components;

// import UtilitiesQL;

// #ifndef MAXCOLUMNS__
//     #define MAXCOLUMNS__ 64
// #endif

// inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

// export enum class txnState : unsigned char { invalid = '0', valid = '1' };

// // Tag used to pick the "load connection string from a config file" constructor.
// // (A bare string literal used to be ambiguous between std::string and
// // std::filesystem::path.)
// export struct ConfigFileTag { explicit ConfigFileTag() = default; };
// export inline constexpr ConfigFileTag configFile{};

// export template <typename Policy = DefaultTaskPolicy>
// class DatabaseComponent : public Component<Policy> {
// public:
//     using QueryResult = std::expected<pqxx::result, std::string>;

//     static constexpr std::string_view DefaultConnectionString{
//         "dbname=postgres user=postgres hostaddr=localhost port=5432"};

//     txnState transactionState{txnState::invalid};

//     explicit DatabaseComponent(DataBus& bus,
//                                std::string connectionString = std::string{DefaultConnectionString})
//         : Component<Policy>(bus), connStr_(std::move(connectionString)) {
//         currentTransaction_.reserve(512);
//         transactionArgs_.reserve(16);
//     }

//     DatabaseComponent(DataBus& bus, ConfigFileTag, const std::filesystem::path& file)
//         : DatabaseComponent(bus, resolveConnectionString(file)) {}

//     // --- Lifecycle ---

//     void start() override {
//         if (this->started_ && isConnected()) {
//             return;
//         }
//         try {
//             if (!isConnected()) {
//                 conn_.emplace(connStr_);
//             }
//             this->started_ = true;
//         } catch (const std::exception& e) {
//             // Deliberately does not print the connection string (may hold credentials).
//             std::println(stderr, "Failed to open database connection: {}", e.what());
//             conn_.reset();
//             this->started_ = false;
//         }
//     }

//     void stop() override {
//         conn_.reset();
//         this->started_ = false;
//     }

//     [[nodiscard]] bool isConnected() const noexcept {
//         return conn_.has_value() && conn_->is_open();
//     }

//     // --- Insert builder ---
//     // Usage: setTableName -> addArgs (column names) -> buildTransactionFromArgsandVals
//     //        then either addVals(...) + processTransaction()   (one row from accumulated values)
//     //        or     writeToDB(...)                             (one row from values passed directly)
//     // The INSERT scaffold is kept after a successful write so it can be reused;
//     // call resetTransactionState() to start over.

//     void setTableName(std::string_view name) { tableName_ = name; }

//     void addArgs(std::initializer_list<std::string_view> argList) {
//         for (std::string_view arg : argList) addArg(arg);
//     }

//     template <typename... Ts>
//     void addVals(Ts&&... vals) {
//         (addVal(std::forward<Ts>(vals)), ...);
//     }

//     void resetTransactionState() {
//         transactionArgs_.clear();
//         clearValues();
//         currentTransaction_.clear();
//         transactionState = txnState::invalid;
//     }

//     void buildTransactionFromArgsandVals() {
//         transactionState = txnState::invalid;
//         currentTransaction_.clear();

//         if (tableName_.empty()) {
//             std::println(stderr, "Table name must be set before building a transaction.");
//             return;
//         }
//         if (!isValidIdentifier(tableName_, /*allowSchema=*/true)) {
//             std::println(stderr, "Invalid table name '{}'.", tableName_);
//             return;
//         }
//         if (transactionArgs_.empty()) {
//             std::println(stderr, "At least one column name is required.");
//             return;
//         }
//         for (const auto& col : transactionArgs_) {
//             if (!isValidIdentifier(col, /*allowSchema=*/false)) {
//                 std::println(stderr, "Invalid column name '{}'.", col);
//                 return;
//             }
//         }
//         if (valCount_ != 0 && valCount_ != transactionArgs_.size()) {
//             std::println(stderr, "Argument and value counts must match ({} columns, {} values).",
//                          transactionArgs_.size(), valCount_);
//             return;
//         }

//         currentTransaction_ += "INSERT INTO ";
//         currentTransaction_ += tableName_;
//         currentTransaction_ += " (";
//         for (std::size_t i = 0; i < transactionArgs_.size(); ++i) {
//             currentTransaction_ += transactionArgs_[i];
//             currentTransaction_ += (i + 1 < transactionArgs_.size()) ? ", " : ") VALUES (";
//         }
//         for (std::size_t i = 0; i < transactionArgs_.size(); ++i) {
//             std::format_to(std::back_inserter(currentTransaction_), "${}", i + 1);
//             currentTransaction_ += (i + 1 < transactionArgs_.size()) ? ", " : ")";
//         }

//         transactionState = txnState::valid;
//     }

//     // --- Execution ---

//     // Insert one row using the values accumulated through addVals().
//     int processTransaction() {
//         if (transactionState != txnState::valid) {
//             std::println(stderr, "No valid transaction has been built.");
//             publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
//             return 1;
//         }
//         if (valCount_ != transactionArgs_.size()) {
//             std::println(stderr, "Expected {} values but {} were added.",
//                          transactionArgs_.size(), valCount_);
//             publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
//             return 1;
//         }
//         if (!run(DatabaseTx::Mode::Write, currentTransaction_, transactionParams_)) {
//             return 1;   // values are kept so the caller may retry
//         }
//         clearValues();
//         return 0;
//     }

//     // Insert one row using the already-built scaffold and values passed right now.
//     template <typename... Args>
//     int writeToDB(Args&&... args) {
//         if (transactionState != txnState::valid) {
//             std::println(stderr, "No valid transaction has been built.");
//             publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
//             return 1;
//         }
//         if (sizeof...(Args) != transactionArgs_.size()) {
//             std::println(stderr, "Expected {} values but got {}.",
//                          transactionArgs_.size(), sizeof...(Args));
//             publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
//             return 1;
//         }
//         pqxx::params params;
//         (appendParam(params, std::forward<Args>(args)), ...);
//         return run(DatabaseTx::Mode::Write, currentTransaction_, params) ? 0 : 1;
//     }

//     // Arbitrary parameterised SQL in a read/write transaction ($1, $2, ...).
//     // Use for DDL, UPDATE/DELETE, INSERT ... RETURNING, etc.
//     template <typename... Args>
//     QueryResult executeWrite(std::string_view sql, Args&&... args) {
//         pqxx::params params;
//         (appendParam(params, std::forward<Args>(args)), ...);
//         return run(DatabaseTx::Mode::Write, std::string{sql}, params);
//     }

//     // Parameterised SELECT in a read-only transaction. Returns the rows.
//     template <typename... Args>
//     QueryResult readFromDB(std::string_view sql, Args&&... args) {
//         pqxx::params params;
//         (appendParam(params, std::forward<Args>(args)), ...);
//         return run(DatabaseTx::Mode::Read, std::string{sql}, params);
//     }

// private:
//     static std::string resolveConnectionString(const std::filesystem::path& file) {
//         using Expected = std::expected<std::string, std::string>;
//         return bpql::loadCredentials(file)
//             .and_then(bpql::validate)
//             .transform(bpql::toConnString)
//             .or_else([](const std::string& err) -> Expected {
//                 std::println(stderr, "Primary config failed ({}), trying fallback...", err);
//                 return bpql::loadCredentials("fallback_creds.txt")
//                     .and_then(bpql::validate)
//                     .transform(bpql::toConnString);
//             })
//             .or_else([](const std::string& err) -> Expected {
//                 std::println(stderr, "Fallback config failed ({}), using default connection string.", err);
//                 return std::string{DefaultConnectionString};
//             })
//             .value_or(std::string{DefaultConnectionString});
//     }

//     // Identifiers are concatenated into SQL (they cannot be bound as parameters),
//     // so only plain [A-Za-z_][A-Za-z0-9_]* names (optionally schema.table) are accepted.
//     static bool isValidIdentifier(std::string_view s, bool allowSchema) {
//         if (s.empty()) return false;
//         bool segmentStart = true;
//         for (char c : s) {
//             const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
//             const bool digit = (c >= '0' && c <= '9');
//             if (c == '.' && allowSchema) {
//                 if (segmentStart) return false;
//                 segmentStart = true;
//             } else if (segmentStart ? alpha : (alpha || digit)) {
//                 segmentStart = false;
//             } else {
//                 return false;
//             }
//         }
//         return !segmentStart;
//     }

//     void addArg(std::string_view arg) {
//         if (transactionArgs_.size() >= MaxCols) {
//             throw std::out_of_range("Exceeded MaxCols limit");
//         }
//         transactionArgs_.emplace_back(arg);   // owned copy: no dangling views
//     }

//     template <typename T>
//     void addVal(T&& val) {
//         appendParam(transactionParams_, std::forward<T>(val));
//         ++valCount_;
//     }

//     // pqxx::params keeps views (not copies) of zview/char* arguments, so string-like
//     // values are copied into a std::string first.
//     template <typename T>
//     static void appendParam(pqxx::params& params, T&& value) {
//         if constexpr (std::is_convertible_v<T, std::string_view>) {
//             params.append(std::string{std::string_view{value}});
//         } else {
//             params.append(std::forward<T>(value));
//         }
//     }

//     void clearValues() {
//         transactionParams_ = pqxx::params{};
//         valCount_ = 0;
//     }

//     // A throwing subscriber must never change the outcome of a database operation.
//     void publishTxStatus(DatabaseTx::Mode mode, DatabaseTx::Status status, std::uint64_t pgXid) noexcept {
//         try {
//             this->bus_.publish(DatabaseTx{.mode = mode, .status = status, .pgXid = pgXid});
//         } catch (const std::exception& e) {
//             std::println(stderr, "DatabaseTx subscriber threw: {}", e.what());
//         } catch (...) {
//             std::println(stderr, "DatabaseTx subscriber threw an unknown exception.");
//         }
//     }

//     QueryResult run(DatabaseTx::Mode mode, const std::string& sql, const pqxx::params& params) {
//         if (!isConnected()) {
//             std::println(stderr, "Database connection is not open.");
//             publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
//             return std::unexpected{std::string{"Database connection is not open."}};
//         }
//         if (sql.empty()) {
//             std::println(stderr, "No SQL to execute.");
//             publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
//             return std::unexpected{std::string{"No SQL to execute."}};
//         }

//         std::uint64_t pgXid = 0;
//         pqxx::result result;

//         try {
//             if (mode == DatabaseTx::Mode::Write) {
//                 pqxx::work txn(*conn_);

//                 // Real 64-bit Postgres xid8 (PostgreSQL 13+; use txid_current() on older servers).
//                 pgXid = txn.query_value<std::uint64_t>("SELECT pg_current_xact_id()::text::bigint");
//                 publishTxStatus(mode, DatabaseTx::Status::Waiting, pgXid);

//                 result = txn.exec(sql, params);
//                 txn.commit();
//             } else {
//                 // Read-only transactions use virtual XIDs (pgXid stays 0).
//                 publishTxStatus(mode, DatabaseTx::Status::Waiting, 0);

//                 pqxx::read_transaction txn(*conn_);
//                 result = txn.exec(sql, params);
//                 txn.commit();
//             }
//         } catch (const std::exception& e) {
//             std::println(stderr, "Database error (XID {}): {}", pgXid, e.what());
//             publishTxStatus(mode, DatabaseTx::Status::Fail, pgXid);
//             return std::unexpected{std::string{e.what()}};
//         }

//         // Outside the try block: the commit succeeded, so this must never report Fail.
//         publishTxStatus(mode, DatabaseTx::Status::Success, pgXid);
//         return result;
//     }

//     std::string currentTransaction_;
//     std::vector<std::string> transactionArgs_;   // owned column names
//     pqxx::params transactionParams_;
//     std::size_t valCount_ = 0;

//     std::string tableName_;
//     std::string connStr_;
//     std::optional<pqxx::connection> conn_;
// };

module;
#include <cstdio>

export module DatabaseComponent;

import std;
import TaskPolicies;
import Pqxx;
export import Components;

import UtilitiesQL;

#ifndef MAXCOLUMNS__
    #define MAXCOLUMNS__ 64
#endif

inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

export enum class txnState : unsigned char { invalid = '0', valid = '1' };

export template <typename Policy = DefaultTaskPolicy> 
class DatabaseComponent : public Component<Policy> {
    using Component<Policy>::Component;
    using active_task_type = decltype(std::declval<TaskExecutor<Policy>&>().run([] {}));

public:
    txnState transactionState{txnState::invalid};

    explicit DatabaseComponent(DataBus& bus, std::string connectionString = "dbname=postgres user=postgres hostaddr=localhost port=5432")
        : Component<>(bus), connStr_(std::move(connectionString)) {
        currentTransaction_.reserve(512);
    }

    explicit DatabaseComponent(DataBus& bus, const std::filesystem::path& configFile)
        : DatabaseComponent(bus, resolveConnectionString(configFile)) {}

    void start() override {
        if (!conn_ || !conn_->is_open()) {
            conn_.emplace(connStr_);
        }
        running = true;
    }

    void stop() override {
        conn_.reset();
        running = false;
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
        this->bus_.template publish(DatabaseTx{
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
    
    std::optional<active_task_type> activeTask_;

    bool running{false};
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
};

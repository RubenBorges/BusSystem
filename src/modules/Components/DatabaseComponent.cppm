module;
#include <cstdio>

export module DatabaseComponent;

import std;
export import Pqxx;       // pqxx::result is part of the public API
export import Components; // also re-exports TaskPolicies, DataBus and EventType
import UtilitiesQL;

#ifndef MAXCOLUMNS__
#define MAXCOLUMNS__ 64
#endif

inline constexpr std::size_t MaxCols{MAXCOLUMNS__};

export enum class txnState : unsigned char { invalid = '0', valid = '1' };

// Selects the "load connection string from a credentials file" constructor
// (a bare string literal would otherwise be ambiguous with std::string).
export struct ConfigFileTag {
  explicit ConfigFileTag() = default;
};
export inline constexpr ConfigFileTag configFile{};

export template <TaskLaunchPolicy ExecPolicy = DefaultTaskPolicy,
                 ErrorHandlingPolicy ErrPolicy = DefaultErrorPolicy>
class DatabaseComponent : public Component<ExecPolicy, ErrPolicy> {
  using Base = Component<ExecPolicy, ErrPolicy>;

public:
  using QueryResult = std::expected<pqxx::result, std::string>;

  static constexpr std::string_view DefaultConnectionString{
      "dbname=postgres user=postgres hostaddr=127.0.0.1 port=5432"};

  explicit DatabaseComponent(DataBus &bus,std::string connectionString = std::string{DefaultConnectionString})
      : Base(bus), connStr_(std::move(connectionString)) {
    currentTransaction_.reserve(512);
    transactionArgs_.reserve(16);
  }

  DatabaseComponent(DataBus &bus, ConfigFileTag, const std::filesystem::path &file): DatabaseComponent(bus, resolveConnectionString(file)) {}

  // --- Lifecycle ---------------------------------------------------------

  void start() override {
    std::lock_guard lock(connMutex_);
    if (this->started_ && isConnected()) return;
    
    try {
      if (!isConnected()) conn_.emplace(connStr_);
      this->started_ = true;
    } 
    catch (const std::exception &e) {
      // Deliberately does not print the connection string (may hold credentials).
      std::println(stderr, "Failed to open database connection: {}", e.what());
      conn_.reset();
      this->started_ = false;
    }
  }

  void stop() override {
    std::lock_guard lock(
        connMutex_); // waits for a submitted job that is mid-flight
    conn_.reset();
    this->started_ = false;
  }

  [[nodiscard]] bool isConnected() const {
    return conn_.has_value() && conn_->is_open();
  }

  // --- Running work through the execution policy --------------------------

  template <typename Job> [[nodiscard]] auto submit(Job &&job) {
    return this->executeTask([this, job = std::forward<Job>(job)]() mutable {
      std::lock_guard lock(connMutex_);
      return job(*this);
    });
  }

  // --- Insert builder (single-threaded) ----------------------------------

  void setTableName(std::string_view name) { tableName_ = name; }

  void addArgs(std::initializer_list<std::string_view> argList) {
    if (transactionArgs_.capacity() <
        transactionArgs_.size() + argList.size()) {
      transactionArgs_.reserve(transactionArgs_.size() + argList.size());
    }
    for (std::string_view arg : argList) {
      addArg(arg);
    }
  }

  template <typename... Ts> void addVals(Ts &&...vals) {
    if (valCount_ == 0 && sizeof...(Ts) > 0) {
      transactionParams_.reserve(sizeof...(Ts));
    }
    (addVal(std::forward<Ts>(vals)), ...);
  }

  void resetTransactionState() {
    transactionArgs_.clear();
    clearValues();
    currentTransaction_.clear();
    transactionState = txnState::invalid;
  }

  void buildTransactionFromArgsandVals() {
    transactionState = txnState::invalid;
    currentTransaction_.clear();

    if (tableName_.empty()) {
      std::println(stderr,
                   "Table name must be set before building a transaction.");
      return;
    }
    if (!bpql::isSqlIdentifier(tableName_, /*allowSchema=*/true)) {
      std::println(stderr, "Invalid table name '{}'.", tableName_);
      return;
    }
    if (transactionArgs_.empty()) {
      std::println(stderr, "At least one column name is required.");
      return;
    }
    for (const auto &column : transactionArgs_) {
      if (!bpql::isSqlIdentifier(column, /*allowSchema=*/false)) {
        std::println(stderr, "Invalid column name '{}'.", column);
        return;
      }
    }
    if (valCount_ != transactionArgs_.size()) {
      std::println(
          stderr,
          "Argument and value counts must match ({} columns, {} values).",
          transactionArgs_.size(), valCount_);
      return;
    }

    // Pre-calculate exact SQL length to avoid dynamic buffer growth
    const std::size_t colCount = transactionArgs_.size();
    std::size_t exactLen =
        12 + tableName_.size() + 2 + 10 +
        1; // "INSERT INTO " + table + " (" + ") VALUES (" + ")"
    for (std::size_t i = 0; i < colCount; ++i) {
      exactLen += transactionArgs_[i].size();
      exactLen += (i + 1 < 10) ? 2 : 3; // "$1".."$9" vs "$10"..
      if (i + 1 < colCount) {
        exactLen += 2;
      }
    }
    currentTransaction_.reserve(exactLen);

    currentTransaction_ += "INSERT INTO ";
    currentTransaction_ += tableName_;
    currentTransaction_ += " (";
    for (const auto [i, col] : std::views::enumerate(transactionArgs_)) {
      currentTransaction_ += col;
      currentTransaction_ +=
          (static_cast<std::size_t>(i) + 1 < colCount) ? ", " : ") VALUES (";
    }
    for (std::size_t i = 0; i < colCount; ++i) {
      std::format_to(std::back_inserter(currentTransaction_), "${}", i + 1);
      currentTransaction_ += (i + 1 < colCount) ? ", " : ")";
    }

    transactionParams_.reserve(colCount);
    transactionState = txnState::valid;
  }

  // --- Execution ---------------------------------------------------------

  // Insert one row using the values accumulated through addVals().
  int processTransaction() {
    if (transactionState != txnState::valid) {
      std::println(stderr, "No valid transaction has been built.");
      publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
      return 1;
    }
if (valCount_ && valCount_ != transactionArgs_.size()) {
          std::println(stderr, "Expected {} values but {} were added.",
                   transactionArgs_.size(), valCount_);
      publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
      return 1;
    }
    if (!run(DatabaseTx::Mode::Write, pqxx::zview{currentTransaction_},
             transactionParams_)) {
      return 1; // values are kept so the caller may retry
    }
    clearValues();
    return 0;
  }

  // Insert one row using the already-built scaffold and values passed right now
  // (zero-copy).
  template <typename... Args> int writeToDB(Args &&...args) {
    if (transactionState != txnState::valid) {
      std::println(stderr, "No valid transaction has been built.");
      publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
      return 1;
    }
    if (sizeof...(Args) != transactionArgs_.size()) {
      std::println(stderr, "Expected {} values but got {}.",
                   transactionArgs_.size(), sizeof...(Args));
      publishTxStatus(DatabaseTx::Mode::Write, DatabaseTx::Status::Fail, 0);
      return 1;
    }
    pqxx::params params;
    params.reserve(sizeof...(Args));
    (appendParamBorrowed(params, std::forward<Args>(args)), ...);
    return run(DatabaseTx::Mode::Write, pqxx::zview{currentTransaction_}, params) ? 0 : 1;
  }

  // Zero-copy overload for null-terminated SQL (string literals, std::string,
  // pqxx::zview).
  template <typename... Args>
  QueryResult executeWrite(pqxx::zview sql, Args &&...args) {
    pqxx::params params;
    params.reserve(sizeof...(Args));
    (appendParamBorrowed(params, std::forward<Args>(args)), ...);
    return run(DatabaseTx::Mode::Write, sql, params);
  }

  // Fallback overload when caller passes a non-null-terminated
  // std::string_view.
  template <typename Sql, typename... Args>
    requires(std::is_same_v<std::remove_cvref_t<Sql>, std::string_view>)
  QueryResult executeWrite(Sql sql, Args &&...args) {
    return executeWrite(std::string{sql}, std::forward<Args>(args)...);
  }

  // Zero-copy overload for null-terminated SELECT SQL.
  template <typename... Args>
  QueryResult readFromDB(pqxx::zview sql, Args &&...args) {
    pqxx::params params;
    params.reserve(sizeof...(Args));
    (appendParamBorrowed(params, std::forward<Args>(args)), ...);
    return run(DatabaseTx::Mode::Read, sql, params);
  }

  // Fallback overload when caller passes a non-null-terminated
  // std::string_view.
  template <typename Sql, typename... Args>
    requires(std::is_same_v<std::remove_cvref_t<Sql>, std::string_view>)
  QueryResult readFromDB(Sql sql, Args &&...args) {
    return readFromDB(std::string{sql}, std::forward<Args>(args)...);
  }

private:
  static std::string resolveConnectionString(const std::filesystem::path &file) {
    using Expected = std::expected<std::string, std::string>;
    return bpql::loadCredentials(file)
        .and_then(bpql::validate)
        .transform(bpql::toConnString)
        .or_else([](const std::string &err) -> Expected {
          std::println(stderr, "Primary config failed ({}), trying fallback...",
                       err);
          return bpql::loadCredentials("fallback_creds.txt")
              .and_then(bpql::validate)
              .transform(bpql::toConnString);
        })
        .or_else([](const std::string &err) -> Expected {
          std::println(
              stderr,
              "Fallback config failed ({}), using default connection string.",
              err);
          return std::string{DefaultConnectionString};
        })
        .value_or(std::string{DefaultConnectionString});
  }

  void addArg(std::string_view arg) {
    if (transactionState == txnState::valid) {
      transactionArgs_.clear();
      currentTransaction_.clear();
      transactionState = txnState::invalid;
      clearValues();
    }
    if (transactionArgs_.size() >= MaxCols) {
      throw std::out_of_range("Exceeded MaxCols limit");
    }
    transactionArgs_.emplace_back(arg); // owned copy: no dangling views
  }

  void addVal(auto &&val) {
    appendParamOwned(transactionParams_, std::forward<decltype(val)>(val));
    ++valCount_;
  }

  // Used when parameters live only for the duration of a single exec() call.
  static void appendParamBorrowed(pqxx::params &params, auto &&value) {
    using ValType = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::is_convertible_v<ValType, std::string_view>) {
      params.append(std::string_view{value});
    } else {
      params.append(std::forward<decltype(value)>(value));
    }
  }

  // Used when parameters are stored in transactionParams_ across calls.
  static void appendParamOwned(pqxx::params &params, auto &&value) {
    using ValType = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::is_convertible_v<ValType, std::string_view>) {
      params.append(std::string{std::string_view{value}});
    } else {
      params.append(std::forward<decltype(value)>(value));
    }
  }

  void clearValues() {
    transactionParams_ = pqxx::params{};
    if (!transactionArgs_.empty()) {
      transactionParams_.reserve(transactionArgs_.size());
    }
    valCount_ = 0;
  }

  void publishTxStatus(DatabaseTx::Mode mode, DatabaseTx::Status status,
                       std::uint64_t pgXid) noexcept {
    if (this->bus_.template subscriberCount<DatabaseTx>() == 0) {
      return;
    }
    try {
      this->bus_.publish(
          DatabaseTx{.mode = mode, .status = status, .pgXid = pgXid});
    } catch (const std::exception &e) {
      std::println(stderr, "Publishing DatabaseTx failed: {}", e.what());
    } catch (...) {
      std::println(stderr,
                   "Publishing DatabaseTx failed with an unknown exception.");
    }
  }

  QueryResult run(DatabaseTx::Mode mode, pqxx::zview sql, const pqxx::params &params) {
    std::lock_guard lock(connMutex_);

    if (!isConnected()) {
      std::println(stderr, "Database connection is not open.");
      publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
      return std::unexpected{std::string{"Database connection is not open."}};
    }
    if (sql.empty()) {
      std::println(stderr, "No SQL to execute.");
      publishTxStatus(mode, DatabaseTx::Status::Fail, 0);
      return std::unexpected{std::string{"No SQL to execute."}};
    }

    const bool trackTx = (this->bus_.template subscriberCount<DatabaseTx>() > 0);
    std::uint64_t pgXid = 0;
    pqxx::result result;

    try {
      if (mode == DatabaseTx::Mode::Write) {
        pqxx::work txn(*conn_);

        if (trackTx) {
          pgXid = txn.query_value<std::uint64_t>(
              "SELECT pg_current_xact_id()::text::bigint");
          publishTxStatus(mode, DatabaseTx::Status::Waiting, pgXid);
        }

        result = txn.exec(sql, params);
        txn.commit();
      } else {
        if (trackTx) {
          publishTxStatus(mode, DatabaseTx::Status::Waiting, 0);
        }

        pqxx::read_transaction txn(*conn_);
        result = txn.exec(sql, params);
        txn.commit();
      }
    } catch (const std::exception &e) {
      std::println(stderr, "Database error (XID {}): {}", pgXid, e.what());
      publishTxStatus(mode, DatabaseTx::Status::Fail, pgXid);
      return std::unexpected{std::string{e.what()}};
    }

    publishTxStatus(mode, DatabaseTx::Status::Success, pgXid);
    return result;
  }

  // Members ordered by alignment/size to minimize struct padding
  std::recursive_mutex connMutex_; // serialises all use of conn_
  std::optional<pqxx::connection> conn_;
  std::string currentTransaction_;
  std::string tableName_;
  std::string connStr_;
  std::vector<std::string> transactionArgs_; // owned column names
  pqxx::params transactionParams_;
  std::size_t valCount_{0};

public:
  txnState transactionState{txnState::invalid};
};

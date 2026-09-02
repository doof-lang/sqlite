#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "doof_runtime.hpp"

namespace {

std::string encodeSqliteError(int code, const std::string& message) {
    return std::to_string(code) + "|" + message;
}

doof::Result<void, std::string> sqliteOk() {
    return doof::Success<void>{};
}

} // namespace

class NativeExecResult {
public:
    NativeExecResult(int64_t rowsAffected, int64_t lastInsertId)
        : rowsAffected_(rowsAffected), lastInsertId_(lastInsertId) {}

    int64_t rowsAffected() const {
        return rowsAffected_;
    }

    int64_t lastInsertId() const {
        return lastInsertId_;
    }

private:
    int64_t rowsAffected_;
    int64_t lastInsertId_;
};

using NativeSqliteBlob = std::shared_ptr<std::vector<uint8_t>>;
using NativeSqliteValue = std::variant<std::monostate, int64_t, double, std::string, NativeSqliteBlob>;
using NativeSqliteRow = std::shared_ptr<doof::ordered_map<std::string, NativeSqliteValue>>;

struct NativeSqliteConnectionState {
    bool open = false;
};

class NativeSqliteStatement {
public:
    NativeSqliteStatement(
        sqlite3_stmt* stmt,
        std::string sql,
        std::shared_ptr<NativeSqliteConnectionState> connectionState
    )
        : stmt_(stmt), sql_(std::move(sql)), connectionState_(std::move(connectionState)) {}

    ~NativeSqliteStatement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    int32_t parameterCount() const {
        return stmt_ != nullptr ? sqlite3_bind_parameter_count(stmt_) : 0;
    }

    doof::Result<void, std::string> bindText(int32_t index, const std::string& value) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        return bindResult(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT));
    }

    doof::Result<void, std::string> bindInt(int32_t index, int32_t value) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        return bindResult(sqlite3_bind_int(stmt_, index, value));
    }

    doof::Result<void, std::string> bindLong(int32_t index, int64_t value) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        return bindResult(sqlite3_bind_int64(stmt_, index, value));
    }

    doof::Result<void, std::string> bindDouble(int32_t index, double value) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        return bindResult(sqlite3_bind_double(stmt_, index, value));
    }

    doof::Result<void, std::string> bindBlob(int32_t index, const NativeSqliteBlob& value) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        const auto& bytes = value != nullptr ? *value : emptyBlob();
        if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return doof::Failure<std::string>{encodeSqliteError(SQLITE_TOOBIG, "BLOB parameter is too large")};
        }

        const void* data = bytes.empty() ? nullptr : static_cast<const void*>(bytes.data());
        return bindResult(sqlite3_bind_blob(stmt_, index, data, static_cast<int>(bytes.size()), SQLITE_TRANSIENT));
    }

    doof::Result<void, std::string> bindNull(int32_t index) {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        return bindResult(sqlite3_bind_null(stmt_, index));
    }

    doof::Result<bool, std::string> step() {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }

        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return doof::Success<bool>{true};
        }
        if (rc == SQLITE_DONE) {
            return doof::Success<bool>{false};
        }
        return doof::Failure<std::string>{makeError(rc)};
    }

    doof::Result<void, std::string> reset() {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }

        const int resetRc = sqlite3_reset(stmt_);
        if (resetRc != SQLITE_OK) {
            return doof::Failure<std::string>{makeError(resetRc)};
        }

        const int clearRc = sqlite3_clear_bindings(stmt_);
        if (clearRc != SQLITE_OK) {
            return doof::Failure<std::string>{makeError(clearRc)};
        }

        return sqliteOk();
    }

    doof::Result<void, std::string> finalize() {
        if (stmt_ == nullptr) {
            return sqliteOk();
        }

        const int rc = sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        if (rc != SQLITE_OK) {
            return doof::Failure<std::string>{encodeSqliteError(rc, sqlite3_errstr(rc))};
        }

        return sqliteOk();
    }

    doof::Result<NativeSqliteRow, std::string> readCurrentRow() {
        if (!isUsable()) {
            return doof::Failure<std::string>{notUsableError()};
        }
        if (sqlite3_data_count(stmt_) == 0) {
            return doof::Failure<std::string>{encodeSqliteError(SQLITE_MISUSE, "statement is not positioned on a row")};
        }

        auto row = std::make_shared<doof::ordered_map<std::string, NativeSqliteValue>>();
        const int count = sqlite3_column_count(stmt_);
        for (int index = 0; index < count; ++index) {
            const char* rawName = sqlite3_column_name(stmt_, index);
            if (rawName == nullptr) {
                return doof::Failure<std::string>{encodeSqliteError(SQLITE_ERROR, "column has no name")};
            }

            std::string name(rawName);
            if (row->find(name) != row->end()) {
                return doof::Failure<std::string>{encodeSqliteError(SQLITE_ERROR, "duplicate column name: " + name)};
            }

            switch (sqlite3_column_type(stmt_, index)) {
                case SQLITE_NULL:
                    row->insert_or_assign(name, NativeSqliteValue(std::monostate{}));
                    break;
                case SQLITE_INTEGER:
                    row->insert_or_assign(name, NativeSqliteValue(static_cast<int64_t>(sqlite3_column_int64(stmt_, index))));
                    break;
                case SQLITE_FLOAT:
                    row->insert_or_assign(name, NativeSqliteValue(sqlite3_column_double(stmt_, index)));
                    break;
                case SQLITE_TEXT: {
                    const auto* text = sqlite3_column_text(stmt_, index);
                    if (text == nullptr) {
                        row->insert_or_assign(name, NativeSqliteValue(std::monostate{}));
                    } else {
                        const int size = sqlite3_column_bytes(stmt_, index);
                        row->insert_or_assign(name, NativeSqliteValue(std::string(reinterpret_cast<const char*>(text), size)));
                    }
                    break;
                }
                case SQLITE_BLOB: {
                    const int size = sqlite3_column_bytes(stmt_, index);
                    const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt_, index));
                    auto bytes = std::make_shared<std::vector<uint8_t>>();
                    if (size > 0) {
                        if (blob == nullptr) {
                            return doof::Failure<std::string>{encodeSqliteError(SQLITE_ERROR, "BLOB column data is unavailable")};
                        }
                        bytes->assign(blob, blob + size);
                    }
                    row->insert_or_assign(name, NativeSqliteValue(bytes));
                    break;
                }
                default:
                    return doof::Failure<std::string>{encodeSqliteError(SQLITE_ERROR, "unsupported sqlite column type")};
            }
        }

        return doof::Success<NativeSqliteRow>{row};
    }

    bool hasResultSet() const {
        return stmt_ != nullptr && sqlite3_column_count(stmt_) > 0;
    }

private:
    bool isUsable() const {
        return stmt_ != nullptr && connectionState_ != nullptr && connectionState_->open;
    }

    std::string notUsableError() const {
        return encodeSqliteError(
            SQLITE_MISUSE,
            stmt_ == nullptr ? "statement is already finalized" : "database is not open"
        );
    }

    static const std::vector<uint8_t>& emptyBlob() {
        static const std::vector<uint8_t> empty;
        return empty;
    }

    doof::Result<void, std::string> bindResult(int rc) {
        if (rc != SQLITE_OK) {
            return doof::Failure<std::string>{makeError(rc)};
        }
        return sqliteOk();
    }

    std::string makeError(int rc) const {
        if (stmt_ != nullptr) {
            sqlite3* db = sqlite3_db_handle(stmt_);
            if (db != nullptr) {
                return encodeSqliteError(rc, sqlite3_errmsg(db));
            }
        }
        return encodeSqliteError(rc, sqlite3_errstr(rc));
    }

    sqlite3_stmt* stmt_ = nullptr;
    std::string sql_;
    std::shared_ptr<NativeSqliteConnectionState> connectionState_;
};

class NativeSqliteDatabase {
public:
    static doof::Result<std::shared_ptr<NativeSqliteDatabase>, std::string> open(const std::string& path) {
        auto database = std::make_shared<NativeSqliteDatabase>();
        const int rc = database->openInternal(path);
        if (rc != SQLITE_OK) {
            return doof::Failure<std::string>{database->notOpenError()};
        }
        return doof::Success<std::shared_ptr<NativeSqliteDatabase>>{database};
    }

    NativeSqliteDatabase() = default;

    ~NativeSqliteDatabase() {
        connectionState_->open = false;
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    doof::Result<std::shared_ptr<NativeExecResult>, std::string> exec(const std::string& sql) {
        if (db_ == nullptr) {
            return doof::Failure<std::string>{notOpenError()};
        }

        char* errorMessage = nullptr;
        const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
        if (rc != SQLITE_OK) {
            const std::string text = errorMessage != nullptr ? std::string(errorMessage) : std::string(sqlite3_errmsg(db_));
            if (errorMessage != nullptr) {
                sqlite3_free(errorMessage);
            }
            return doof::Failure<std::string>{encodeSqliteError(rc, text)};
        }

        return doof::Success<std::shared_ptr<NativeExecResult>>{std::make_shared<NativeExecResult>(static_cast<int64_t>(sqlite3_changes(db_)), sqlite3_last_insert_rowid(db_))};
    }

    doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string> prepare(const std::string& sql) {
        if (db_ == nullptr) {
            return doof::Failure<std::string>{notOpenError()};
        }

        sqlite3_stmt* stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return doof::Failure<std::string>{encodeSqliteError(rc, sqlite3_errmsg(db_))};
        }
        if (stmt == nullptr) {
            return doof::Failure<std::string>{encodeSqliteError(SQLITE_MISUSE, "SQL did not contain a statement")};
        }

        return doof::Success<std::shared_ptr<NativeSqliteStatement>>{
            std::make_shared<NativeSqliteStatement>(stmt, sql, connectionState_)
        };
    }

    doof::Result<void, std::string> close() {
        if (db_ == nullptr) {
            return sqliteOk();
        }

        const int rc = sqlite3_close_v2(db_);
        if (rc != SQLITE_OK) {
            return doof::Failure<std::string>{encodeSqliteError(rc, sqlite3_errmsg(db_))};
        }

        db_ = nullptr;
        connectionState_->open = false;
        return sqliteOk();
    }

    int64_t rowsAffected() const {
        if (db_ == nullptr) {
            return 0;
        }
        return static_cast<int64_t>(sqlite3_changes(db_));
    }

    int64_t lastInsertId() const {
        if (db_ == nullptr) {
            return 0;
        }
        return sqlite3_last_insert_rowid(db_);
    }

private:
    int openInternal(const std::string& path) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open(path.c_str(), &raw);
        if (rc != SQLITE_OK) {
            const std::string message = raw != nullptr ? sqlite3_errmsg(raw) : std::string("failed to open sqlite database");
            openError_ = encodeSqliteError(rc, message + " (" + path + ")");
            if (raw != nullptr) {
                sqlite3_close(raw);
            }
            return rc;
        }

        db_ = raw;
        connectionState_->open = true;
        openError_ = std::nullopt;
        return SQLITE_OK;
    }

    std::string notOpenError() const {
        return openError_.value_or(encodeSqliteError(SQLITE_MISUSE, "database is not open"));
    }

    sqlite3* db_ = nullptr;
    std::optional<std::string> openError_;
    std::shared_ptr<NativeSqliteConnectionState> connectionState_ = std::make_shared<NativeSqliteConnectionState>();
};

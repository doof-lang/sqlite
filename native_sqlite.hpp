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
    return doof::Result<void, std::string>::success();
}

} // namespace

class NativeExecResult {
public:
    NativeExecResult(int32_t changes, int64_t lastInsertRowId)
        : changes_(changes), lastInsertRowId_(lastInsertRowId) {}

    int32_t changes() const {
        return changes_;
    }

    int64_t lastInsertRowId() const {
        return lastInsertRowId_;
    }

private:
    int32_t changes_;
    int64_t lastInsertRowId_;
};

using NativeSqliteBlob = std::shared_ptr<std::vector<uint8_t>>;
using NativeSqliteValue = std::variant<std::monostate, int64_t, double, std::string, NativeSqliteBlob>;
using NativeSqliteRow = std::shared_ptr<doof::ordered_map<std::string, NativeSqliteValue>>;

class NativeSqliteStatement {
public:
    NativeSqliteStatement(sqlite3_stmt* stmt, std::string sql)
        : stmt_(stmt), sql_(std::move(sql)) {}

    ~NativeSqliteStatement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    doof::Result<void, std::string> bindText(int32_t index, const std::string& value) {
        return bindResult(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT));
    }

    doof::Result<void, std::string> bindInt(int32_t index, int32_t value) {
        return bindResult(sqlite3_bind_int(stmt_, index, value));
    }

    doof::Result<void, std::string> bindLong(int32_t index, int64_t value) {
        return bindResult(sqlite3_bind_int64(stmt_, index, value));
    }

    doof::Result<void, std::string> bindDouble(int32_t index, double value) {
        return bindResult(sqlite3_bind_double(stmt_, index, value));
    }

    doof::Result<void, std::string> bindBlob(int32_t index, const NativeSqliteBlob& value) {
        const auto& bytes = value != nullptr ? *value : emptyBlob();
        if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return doof::Result<void, std::string>::failure(encodeSqliteError(SQLITE_TOOBIG, "BLOB parameter is too large"));
        }

        const void* data = bytes.empty() ? nullptr : static_cast<const void*>(bytes.data());
        return bindResult(sqlite3_bind_blob(stmt_, index, data, static_cast<int>(bytes.size()), SQLITE_TRANSIENT));
    }

    doof::Result<void, std::string> bindNull(int32_t index) {
        return bindResult(sqlite3_bind_null(stmt_, index));
    }

    doof::Result<bool, std::string> step() {
        if (stmt_ == nullptr) {
            return doof::Result<bool, std::string>::failure(encodeSqliteError(SQLITE_MISUSE, "statement is already finalized"));
        }

        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return doof::Result<bool, std::string>::success(true);
        }
        if (rc == SQLITE_DONE) {
            return doof::Result<bool, std::string>::success(false);
        }
        return doof::Result<bool, std::string>::failure(makeError(rc));
    }

    doof::Result<void, std::string> reset() {
        if (stmt_ == nullptr) {
            return doof::Result<void, std::string>::failure(encodeSqliteError(SQLITE_MISUSE, "statement is already finalized"));
        }

        const int resetRc = sqlite3_reset(stmt_);
        if (resetRc != SQLITE_OK) {
            return doof::Result<void, std::string>::failure(makeError(resetRc));
        }

        const int clearRc = sqlite3_clear_bindings(stmt_);
        if (clearRc != SQLITE_OK) {
            return doof::Result<void, std::string>::failure(makeError(clearRc));
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
            return doof::Result<void, std::string>::failure(encodeSqliteError(rc, sqlite3_errstr(rc)));
        }

        return sqliteOk();
    }

    doof::Result<NativeSqliteRow, std::string> readCurrentRow() {
        if (stmt_ == nullptr) {
            return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_MISUSE, "statement is already finalized"));
        }
        if (sqlite3_data_count(stmt_) == 0) {
            return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_MISUSE, "statement is not positioned on a row"));
        }

        auto row = std::make_shared<doof::ordered_map<std::string, NativeSqliteValue>>();
        const int count = sqlite3_column_count(stmt_);
        for (int index = 0; index < count; ++index) {
            const char* rawName = sqlite3_column_name(stmt_, index);
            if (rawName == nullptr) {
                return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_ERROR, "column has no name"));
            }

            std::string name(rawName);
            if (row->find(name) != row->end()) {
                return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_ERROR, "duplicate column name: " + name));
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
                            return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_ERROR, "BLOB column data is unavailable"));
                        }
                        bytes->assign(blob, blob + size);
                    }
                    row->insert_or_assign(name, NativeSqliteValue(bytes));
                    break;
                }
                default:
                    return doof::Result<NativeSqliteRow, std::string>::failure(encodeSqliteError(SQLITE_ERROR, "unsupported sqlite column type"));
            }
        }

        return doof::Result<NativeSqliteRow, std::string>::success(row);
    }

private:
    static const std::vector<uint8_t>& emptyBlob() {
        static const std::vector<uint8_t> empty;
        return empty;
    }

    doof::Result<void, std::string> bindResult(int rc) {
        if (rc != SQLITE_OK) {
            return doof::Result<void, std::string>::failure(makeError(rc));
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
};

class NativeSqliteDatabase {
public:
    static doof::Result<std::shared_ptr<NativeSqliteDatabase>, std::string> open(const std::string& path) {
        auto database = std::make_shared<NativeSqliteDatabase>();
        const int rc = database->openInternal(path);
        if (rc != SQLITE_OK) {
            return doof::Result<std::shared_ptr<NativeSqliteDatabase>, std::string>::failure(database->notOpenError());
        }
        return doof::Result<std::shared_ptr<NativeSqliteDatabase>, std::string>::success(database);
    }

    NativeSqliteDatabase() = default;

    ~NativeSqliteDatabase() {
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    doof::Result<std::shared_ptr<NativeExecResult>, std::string> exec(const std::string& sql) {
        if (db_ == nullptr) {
            return doof::Result<std::shared_ptr<NativeExecResult>, std::string>::failure(notOpenError());
        }

        char* errorMessage = nullptr;
        const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
        if (rc != SQLITE_OK) {
            const std::string text = errorMessage != nullptr ? std::string(errorMessage) : std::string(sqlite3_errmsg(db_));
            if (errorMessage != nullptr) {
                sqlite3_free(errorMessage);
            }
            return doof::Result<std::shared_ptr<NativeExecResult>, std::string>::failure(encodeSqliteError(rc, text));
        }

        return doof::Result<std::shared_ptr<NativeExecResult>, std::string>::success(
            std::make_shared<NativeExecResult>(sqlite3_changes(db_), sqlite3_last_insert_rowid(db_))
        );
    }

    doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string> prepare(const std::string& sql) {
        if (db_ == nullptr) {
            return doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string>::failure(notOpenError());
        }

        sqlite3_stmt* stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string>::failure(encodeSqliteError(rc, sqlite3_errmsg(db_)));
        }
        if (stmt == nullptr) {
            return doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string>::failure(encodeSqliteError(SQLITE_MISUSE, "SQL did not contain a statement"));
        }

        return doof::Result<std::shared_ptr<NativeSqliteStatement>, std::string>::success(
            std::make_shared<NativeSqliteStatement>(stmt, sql)
        );
    }

    doof::Result<void, std::string> close() {
        if (db_ == nullptr) {
            return sqliteOk();
        }

        const int rc = sqlite3_close_v2(db_);
        if (rc != SQLITE_OK) {
            return doof::Result<void, std::string>::failure(encodeSqliteError(rc, sqlite3_errmsg(db_)));
        }

        db_ = nullptr;
        return sqliteOk();
    }

    int32_t changes() const {
        if (db_ == nullptr) {
            return 0;
        }
        return sqlite3_changes(db_);
    }

    int64_t lastInsertRowId() const {
        if (db_ == nullptr) {
            return 0;
        }
        return sqlite3_last_insert_rowid(db_);
    }

private:
    int openInternal(const std::string& path) {
        path_ = path;
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
        openError_ = std::nullopt;
        return SQLITE_OK;
    }

    std::string notOpenError() const {
        return openError_.value_or(encodeSqliteError(SQLITE_MISUSE, "database is not open"));
    }

    sqlite3* db_ = nullptr;
    std::string path_;
    std::optional<std::string> openError_;
};

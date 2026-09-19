// Thin SQLite wrapper for Doof programs.

export type SqliteParam = int | long | bool | double | string | readonly byte[] | none
export type SqliteValue = long | double | string | readonly byte[] | none

export import class NativeSqliteDatabase from "./native_sqlite.hpp" {
  isolated static open(path: string): Result<NativeSqliteDatabase, string>
  isolated exec(sql: string): Result<NativeExecResult, string>
  isolated prepare(sql: string): Result<NativeSqliteStatement, string>
  isolated close(): Result<none, string>
  isolated rowsAffected(): long
  isolated lastInsertId(): long
}

export import class NativeExecResult from "./native_sqlite.hpp" {
  isolated rowsAffected(): long
  isolated lastInsertId(): long
}

export import class NativeSqliteStatement from "./native_sqlite.hpp" {
  isolated parameterCount(): int
  isolated bindText(index: int, value: string): Result<none, string>
  isolated bindInt(index: int, value: int): Result<none, string>
  isolated bindLong(index: int, value: long): Result<none, string>
  isolated bindDouble(index: int, value: double): Result<none, string>
  isolated bindBlob(index: int, value: readonly byte[]): Result<none, string>
  isolated bindNull(index: int): Result<none, string>
  isolated step(): Result<bool, string>
  isolated readCurrentRow(): Result<Map<string, SqliteValue>, string>
  isolated reset(): Result<none, string>
  isolated finalize(): Result<none, string>
  isolated hasResultSet(): bool
}

export class SqliteError {
  stage: string
  code: string | none
  sqlState: string | none
  message: string
  detail: string | none
  sql: string | none
}

export class ExecResult {
  rowsAffected: long
  lastInsertId: long
}

export class Database {
  native: NativeSqliteDatabase
}

export class Statement {
  database: Database
  native: NativeSqliteStatement
  sql: string
}

export function open(path: string): Result<Database, SqliteError> {
  return case NativeSqliteDatabase.open(path) {
    s: Success -> Success {
      value: Database {
        native: s.value,
      }
    },
    f: Failure -> Failure {
      error: decodeError("open", f.error, none)
    }
  }
}

export function close(database: Database): Result<none, SqliteError> {
  return mapNativeVoid("close", none, database.native.close())
}

function decodeError(stage: string, raw: string, sql: string | none): SqliteError {
  separator := raw.indexOf("|")
  if separator < 0 {
    return SqliteError {
      stage,
      code: none,
      sqlState: none,
      message: raw,
      detail: none,
      sql,
    }
  }

  codeText := raw.substring(0, separator)
  message := raw.slice(separator + 1)
  return SqliteError {
    stage,
    code: if codeText.length > 0 then codeText else none,
    sqlState: none,
    message,
    detail: none,
    sql,
  }
}

function mapNativeVoid(stage: string, sql: string | none, result: Result<none, string>): Result<none, SqliteError> {
  return case result {
    _: Success -> Success(),
    f: Failure -> Failure {
      error: decodeError(stage, f.error, sql)
    }
  }
}

function unexpectedRowError(sql: string): SqliteError {
  return SqliteError {
    stage: "step",
    code: none,
    sqlState: none,
    message: "Statement unexpectedly produced a row",
    detail: none,
    sql,
  }
}

function toExecResult(result: NativeExecResult): ExecResult {
  return ExecResult {
    rowsAffected: result.rowsAffected(),
    lastInsertId: result.lastInsertId(),
  }
}

function emptyRow(): Map<string, SqliteValue> | none {
  return none
}

function readCurrentRow(statement: Statement): Result<Map<string, SqliteValue>, SqliteError> {
  return case statement.native.readCurrentRow() {
    s: Success -> Success {
      value: s.value
    },
    f: Failure -> Failure {
      error: decodeError("read", f.error, statement.sql)
    }
  }
}

export function prepare(database: Database, sql: string): Result<Statement, SqliteError> {
  return case database.native.prepare(sql) {
    s: Success -> Success {
      value: Statement {
        database,
        native: s.value,
        sql,
      }
    },
    f: Failure -> Failure {
      error: decodeError("prepare", f.error, sql)
    }
  }
}

function bindText(statement: Statement, index: int, value: string): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindText(index, value))
}

function bindInt(statement: Statement, index: int, value: int): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindInt(index, value))
}

function bindLong(statement: Statement, index: int, value: long): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindLong(index, value))
}

function bindDouble(statement: Statement, index: int, value: double): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindDouble(index, value))
}

function bindBlob(statement: Statement, index: int, value: readonly byte[]): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindBlob(index, value))
}

function bindNull(statement: Statement, index: int): Result<none, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindNull(index))
}

function bindValue(statement: Statement, index: int, value: SqliteParam): Result<none, SqliteError> {
  return case value {
    text: string -> bindText(statement, index, text),
    flag: bool -> bindInt(statement, index, if flag then 1 else 0),
    number: int -> bindInt(statement, index, number),
    whole: long -> bindLong(statement, index, whole),
    decimal: double -> bindDouble(statement, index, decimal),
    bytes: readonly byte[] -> bindBlob(statement, index, bytes),
    _ -> bindNull(statement, index)
  }
}

function bindValues(statement: Statement, values: SqliteParam[] = []): Result<none, SqliteError> {
  expected := statement.native.parameterCount()
  if values.length != expected {
    return Failure {
      error: SqliteError {
        stage: "bind",
        code: none,
        sqlState: none,
        message: "Expected ${expected} parameters, received ${values.length}",
        detail: none,
        sql: statement.sql,
      }
    }
  }

  for index of 0..<values.length {
    try bindValue(statement, index + 1, values[index])
  }

  return Success()
}

function reset(statement: Statement): Result<none, SqliteError> {
  return mapNativeVoid("reset", statement.sql, statement.native.reset())
}

function step(statement: Statement): Result<Map<string, SqliteValue> | none, SqliteError> {
  case statement.native.step() {
    s: Success -> {
      if s.value {
        case readCurrentRow(statement) {
          row: Success -> return Success { value: row.value }
          error: Failure -> return Failure { error: error.error }
        }
      }
      return Success { value: emptyRow() }
    }
    f: Failure -> return Failure {
      error: decodeError("step", f.error, statement.sql)
    }
  }
}

class RowStream implements Stream<Result<Map<string, SqliteValue>, SqliteError> > {
  statement: Statement
  let currentRow: Map<string, SqliteValue> = {}
  let currentError: SqliteError | none = none
  let finished = false

  next(): bool {
    if finished {
      return false
    }

    case statement.native.step() {
      s: Success -> {
        if s.value {
          case readCurrentRow(statement) {
            row: Success -> {
              this.currentRow = row.value
              this.currentError = none
            }
            err: Failure -> {
              this.currentError = err.error
              this.finished = true
            }
          }
          return true
        } else {
          this.finished = true
          return false
        }
      }
      f: Failure -> {
        this.currentError = decodeError("step", f.error, statement.sql)
        this.finished = true
        return true
      }
    }
  }

  value(): Result<Map<string, SqliteValue>, SqliteError> {
    if this.currentError != none {
      return Failure { error: this.currentError! }
    }
    return Success { value: this.currentRow }
  }
}

export function query(statement: Statement, values: SqliteParam[] = []): Result<Stream<Result<Map<string, SqliteValue>, SqliteError> >, SqliteError> {
  try reset(statement)
  try bindValues(statement, values)
  return Success { value: RowStream(statement) }
}

export function execute(statement: Statement, values: SqliteParam[] = []): Result<ExecResult, SqliteError> {
  try reset(statement)
  try bindValues(statement, values)
  try row := step(statement)

  if row != none || statement.native.hasResultSet() {
    return Failure {
      error: unexpectedRowError(statement.sql)
    }
  }

  return Success {
    value: ExecResult {
      rowsAffected: statement.database.native.rowsAffected(),
      lastInsertId: statement.database.native.lastInsertId(),
    }
  }
}

export function executeSql(database: Database, sql: string): Result<ExecResult, SqliteError> {
  return case database.native.exec(sql) {
    s: Success -> Success {
      value: toExecResult(s.value)
    },
    f: Failure -> Failure {
      error: decodeError("execute", f.error, sql)
    }
  }
}

export function queryOne(statement: Statement, values: SqliteParam[] = []): Result<Map<string, SqliteValue> | none, SqliteError> {
  try stream := query(statement, values)
  if !stream.next() {
    return Success { value: emptyRow() }
  }

  case stream.value() {
    s: Success -> return Success { value: s.value }
    f: Failure -> return Failure { error: f.error }
  }
}

function toSerialValue(value: SqliteValue): SerialValue {
  case value {
    whole: long -> return whole
    decimal: double -> return decimal
    text: string -> return text
    _ -> return none
  }
}

export function toJsonRow(row: Map<string, SqliteValue>): Map<string, SerialValue> {
  jsonRow: Map<string, SerialValue> := {}
  for key, value of row {
    jsonRow[key] = toSerialValue(value)
  }
  return jsonRow
}

export function begin(database: Database): Result<none, SqliteError> {
  try executeSql(database, "BEGIN TRANSACTION")
  return Success()
}

export function commit(database: Database): Result<none, SqliteError> {
  try executeSql(database, "COMMIT")
  return Success()
}

export function rollback(database: Database): Result<none, SqliteError> {
  try executeSql(database, "ROLLBACK")
  return Success()
}

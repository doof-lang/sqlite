// Thin SQLite wrapper for Doof programs.

export type SqliteParam = int | long | bool | double | string | readonly byte[] | null
export type SqliteValue = long | double | string | readonly byte[] | null

export import class NativeSqliteDatabase from "./native_sqlite.hpp" {
  static open(path: string): Result<NativeSqliteDatabase, string>
  exec(sql: string): Result<NativeExecResult, string>
  prepare(sql: string): Result<NativeSqliteStatement, string>
  close(): Result<void, string>
  changes(): int
  lastInsertRowId(): long
}

export import class NativeExecResult from "./native_sqlite.hpp" {
  changes(): int
  lastInsertRowId(): long
}

export import class NativeSqliteStatement from "./native_sqlite.hpp" {
  bindText(index: int, value: string): Result<void, string>
  bindInt(index: int, value: int): Result<void, string>
  bindLong(index: int, value: long): Result<void, string>
  bindDouble(index: int, value: double): Result<void, string>
  bindBlob(index: int, value: readonly byte[]): Result<void, string>
  bindNull(index: int): Result<void, string>
  step(): Result<bool, string>
  readCurrentRow(): Result<Map<string, SqliteValue>, string>
  reset(): Result<void, string>
  finalize(): Result<void, string>
}

export class SqliteError {
  stage: string
  code: int
  message: string
  sql: string | null
}

export class ExecResult {
  changes: int
  lastInsertRowId: long
}

export class Database {
  native: NativeSqliteDatabase
  path: string
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
        path,
      }
    },
    f: Failure -> Failure {
      error: decodeError("open", f.error, null)
    }
  }
}

export function close(database: Database): Result<void, SqliteError> {
  return mapNativeVoid("close", null, database.native.close())
}

function decodeError(stage: string, raw: string, sql: string | null): SqliteError {
  separator := raw.indexOf("|")
  if separator < 0 {
    return SqliteError {
      stage,
      code: 0,
      message: raw,
      sql,
    }
  }

  codeText := raw.substring(0, separator)
  message := raw.slice(separator + 1)
  code := try? int.parse(codeText) ?? 0
  return SqliteError {
    stage,
    code,
    message,
    sql,
  }
}

function mapNativeVoid(stage: string, sql: string | null, result: Result<void, string>): Result<void, SqliteError> {
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
    code: 0,
    message: "Statement unexpectedly produced a row",
    sql,
  }
}

function toExecResult(result: NativeExecResult): ExecResult {
  return ExecResult {
    changes: result.changes(),
    lastInsertRowId: result.lastInsertRowId(),
  }
}

function emptyRow(): Map<string, SqliteValue> | null {
  return null
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

function bindText(statement: Statement, index: int, value: string): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindText(index, value))
}

function bindInt(statement: Statement, index: int, value: int): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindInt(index, value))
}

function bindLong(statement: Statement, index: int, value: long): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindLong(index, value))
}

function bindDouble(statement: Statement, index: int, value: double): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindDouble(index, value))
}

function bindBlob(statement: Statement, index: int, value: readonly byte[]): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindBlob(index, value))
}

function bindNull(statement: Statement, index: int): Result<void, SqliteError> {
  return mapNativeVoid("bind", statement.sql, statement.native.bindNull(index))
}

function bindValue(statement: Statement, index: int, value: SqliteParam): Result<void, SqliteError> {
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

function bindValues(statement: Statement, values: SqliteParam[] = []): Result<void, SqliteError> {
  for index of 0..<values.length {
    try bindValue(statement, index + 1, values[index])
  }

  return Success()
}

function reset(statement: Statement): Result<void, SqliteError> {
  return mapNativeVoid("reset", statement.sql, statement.native.reset())
}

function step(statement: Statement): Result<Map<string, SqliteValue> | null, SqliteError> {
  return case statement.native.step() {
    s: Success -> if s.value then readCurrentRow(statement) else Success {
      value: emptyRow()
    },
    f: Failure -> Failure {
      error: decodeError("step", f.error, statement.sql)
    }
  }
}

class RowStream {
  statement: Statement

  next(): Result<Map<string, SqliteValue>, SqliteError> | null {
    case statement.native.step() {
      s: Success -> {
        if s.value {
          return readCurrentRow(statement)
        } else {
          return null
        }
      }
      f: Failure -> return Failure {
        error: decodeError("step", f.error, statement.sql)
      }
    }
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

  if row != null {
    return Failure {
      error: unexpectedRowError(statement.sql)
    }
  }

  return Success {
    value: ExecResult {
      changes: statement.database.native.changes(),
      lastInsertRowId: statement.database.native.lastInsertRowId(),
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

export function queryOne(statement: Statement, values: SqliteParam[] = []): Result<Map<string, SqliteValue> | null, SqliteError> {
  try stream := query(statement, values)
  n := stream.next()
  if n == null {
    return Success { value: emptyRow() }
  }

  case n! {
    s: Success -> return Success { value: s.value }
    f: Failure -> return Failure { error: f.error }
  }
}

function toJsonValue(value: SqliteValue): JsonValue {
  case value {
    whole: long -> return whole
    decimal: double -> return decimal
    text: string -> return text
    _ -> return null
  }
}

export function toJsonRow(row: Map<string, SqliteValue>): Map<string, JsonValue> {
  jsonRow: Map<string, JsonValue> := {}
  for key, value of row {
    jsonRow[key] = toJsonValue(value)
  }
  return jsonRow
}

export function begin(database: Database): Result<void, SqliteError> {
  try executeSql(database, "BEGIN TRANSACTION")
  return Success()
}

export function commit(database: Database): Result<void, SqliteError> {
  try executeSql(database, "COMMIT")
  return Success()
}

export function rollback(database: Database): Result<void, SqliteError> {
  try executeSql(database, "ROLLBACK")
  return Success()
}

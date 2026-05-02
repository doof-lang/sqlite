# std/sqlite

Small `Result`-first SQLite wrapper for Doof programs. It supports opening SQLite databases, executing SQL, prepared statements with positional parameters, streaming result rows, single-row queries, and basic transaction helpers.

## Usage

```doof
import { execute, executeSql, open, prepare, query, toJsonRow } from "std/sqlite"

class Todo {
  id: int
  title: string
  done: bool
}

database := try open(":memory:")

try executeSql(database, `CREATE TABLE todos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  done INTEGER NOT NULL
)`)

insertTodo := try prepare(database, "INSERT INTO todos(title, done) VALUES (?, ?)")
try execute(insertTodo, ["Ship the sqlite wrapper", true])
try execute(insertTodo, ["Write the README", true])

selectTodos := try prepare(database, "SELECT id, title, done FROM todos ORDER BY id")
stream := try query(selectTodos)

for item of stream {
  row := try item
  todo := try Todo.fromJsonValue(toJsonRow(row), true)
  println("#${todo.id}: ${todo.title}")
}
```

## Values

SQLite parameters accept `int | long | bool | double | string | readonly byte[] | null`.

Rows return `Map<string, SqliteValue>`, where `SqliteValue` is `long | double | string | readonly byte[] | null`. SQLite integers are read as `long`; BLOB columns are read as `readonly byte[]`; booleans are conventionally stored as `0` or `1` and can be mapped through `toJsonRow(...)` into Doof classes with `bool` fields.

`toJsonRow(...)` converts BLOB values to null.

## API

### `open(path: string): Result<Database, SqliteError>`

Open a database file. Use `":memory:"` for an in-memory database.

### `close(database: Database): Result<void, SqliteError>`

Close a database. Calling `close` more than once is safe.

### `executeSql(database: Database, sql: string): Result<ExecResult, SqliteError>`

Execute one or more SQL statements directly with SQLite. This is useful for schema setup, pragmas, and transaction control.

`ExecResult` contains:

| Field | Type | Description |
|-------|------|-------------|
| `changes` | `int` | Rows changed by the statement |
| `lastInsertRowId` | `long` | SQLite last insert rowid |

### `prepare(database: Database, sql: string): Result<Statement, SqliteError>`

Compile a reusable statement. Parameters use SQLite's positional `?` placeholders and are bound with one-based indexes internally.

### `execute(statement: Statement, values: SqliteParam[] = []): Result<ExecResult, SqliteError>`

Reset, bind, and run a prepared statement that should not return rows. If the statement does produce a row, `execute` fails so accidental `SELECT` calls do not silently discard data.

```doof
insertUser := try prepare(database, "INSERT INTO users(name, active) VALUES (?, ?)")
result := try execute(insertUser, ["Ada", true])
println("inserted row ${result.lastInsertRowId}")
```

BLOB parameters can be bound directly as byte arrays:

```doof
payload: readonly byte[] := [0, 1, 2, 255]
insertFile := try prepare(database, "INSERT INTO files(payload) VALUES (?)")
try execute(insertFile, [payload])
```

### `query(statement: Statement, values: SqliteParam[] = []): Result<Stream<Result<Map<string, SqliteValue>, SqliteError>>, SqliteError>`

Reset, bind, and stream all rows from a prepared statement. Each stream item is a `Result`, so row conversion errors are handled at the point the row is read.

```doof
statement := try prepare(database, "SELECT id, name FROM users WHERE active = ?")
stream := try query(statement, [true])

for item of stream {
  row := try item
  println(row)
}
```

### `queryOne(statement: Statement, values: SqliteParam[] = []): Result<Map<string, SqliteValue> | null, SqliteError>`

Return the first row from a query, or `null` if there are no rows. Additional rows are ignored.

### `begin(database)`, `commit(database)`, `rollback(database)`

Convenience helpers for `BEGIN TRANSACTION`, `COMMIT`, and `ROLLBACK`.

```doof
try begin(database)
try execute(insertUser, ["Grace", true])
try commit(database)
```

## Errors

All public operations return `Result<_, SqliteError>`. `SqliteError` includes:

| Field | Type | Description |
|-------|------|-------------|
| `stage` | `string` | Operation stage such as `open`, `prepare`, `bind`, `step`, or `read` |
| `code` | `int` | SQLite result code when available |
| `message` | `string` | Human-readable error message |
| `sql` | `string | null` | SQL text associated with the error when available |

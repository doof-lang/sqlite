# std/sqlite Guide

`std/sqlite` is a small `Result`-first SQLite wrapper. It supports database
files and `:memory:`, direct SQL execution, prepared statements, streaming rows,
single-row queries, value conversion, and transaction helpers.

## Lifecycle

Open a database with `open(path)` and close it with `close(database)` when done.
Closing more than once is safe.

Use `executeSql` for schema setup, pragmas, and transaction control. Use
`prepare` plus `execute`, `query`, or `queryOne` for repeated statements.

## Parameters And Rows

Parameters accept:

```doof
int | long | bool | double | string | readonly byte[] | null
```

Rows are `Map<string, SqliteValue>`, where SQLite integers are returned as
`long`, floating point values as `double`, text as `string`, blobs as
`readonly byte[]`, and nulls as `null`.

`toJsonRow(row)` converts rows into JSON-friendly values. Blob values become
`null`, which keeps typed `.fromJsonValue(...)` decoding predictable.

## Streaming Queries

`query(statement, values)` returns a stream whose items are themselves
`Result<Map<string, SqliteValue>, SqliteError>`. Opening and binding failures
are returned immediately; row-reading failures are reported at the point the row
is pulled.

`queryOne` returns the first row or `null` and ignores additional rows.

## Transactions

`begin`, `commit`, and `rollback` are convenience wrappers around SQLite
transaction SQL. Use explicit rollback in failure paths when a transaction spans
multiple calls.

## API Map

Types:

- `SqliteParam`
- `SqliteValue`
- `SqliteError`
- `Database`
- `Statement`
- `ExecResult`

Lifecycle and statements:

- `open`
- `close`
- `prepare`
- `executeSql`
- `execute`
- `query`
- `queryOne`

Conversion and transactions:

- `toJsonRow`
- `begin`
- `commit`
- `rollback`

Declarations are defined in [index.do](../index.do).

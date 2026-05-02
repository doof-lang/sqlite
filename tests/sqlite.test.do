import {
  Database,
  SqliteError,
  SqliteValue,
  execute,
  executeSql,
  open,
  prepare,
  query,
  queryOne,
  toJsonRow,
} from "../index"

class Person {
  id: int
  name: string
  score: int
  active: bool
}

class BlobRecord {
  id: int
  payload: readonly byte[]
}

function assertBytes(actual: readonly byte[], expected: readonly byte[]): void {
  assert(actual.length == expected.length, "expected blob lengths to match")

  for index of 0..<actual.length {
    assert(actual[index] == expected[index], "expected blob bytes to match")
  }
}

function columnValue(row: Map<string, SqliteValue>, name: string): SqliteValue {
  for key, value of row {
    if key == name {
      return value
    }
  }

  assert(false, "expected sqlite row column ${name}")
  return null
}

function assertBlobValue(value: SqliteValue, expected: readonly byte[]): void {
  case value {
    actual: readonly byte[] -> assertBytes(actual, expected)
    _ -> assert(false, "expected sqlite value to be a blob")
  }
}

function createPeopleDatabase(): Result<Database, SqliteError> {
  try database := open(":memory:")
  try executeSql(database, `CREATE TABLE people (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    score INTEGER NOT NULL,
    active INTEGER NOT NULL
  )`)
  return Success { value: database }
}

function insertPerson(database: Database, name: string, score: int, active: bool): Result<void, SqliteError> {
  try statement := prepare(database, "INSERT INTO people(name, score, active) VALUES (?, ?, ?)")
  try execute(statement, [name, score, active])
  return Success()
}

function readPerson(row: Map<string, SqliteValue>): Person {
  return try! Person.fromJsonValue(toJsonRow(row), true)
}

export function testExecuteAndQuery(): void {
  database := try! createPeopleDatabase()
  try! insertPerson(database, "Ada", 99, true)
  try! insertPerson(database, "Grace", 95, false)

  statement := try! prepare(database, "SELECT id, name, score, active FROM people ORDER BY id")
  stream := try! query(statement)
  people: Person[] := []

  for item of stream {
    row := try! item
    people.push(readPerson(row))
  }

  assert(people.length == 2, "expected two people")
  assert(people[0].name == "Ada", "expected first person name")
  assert(people[0].score == 99, "expected first person score")
  assert(people[0].active, "expected first person to be active")
  assert(people[1].name == "Grace", "expected second person name")
  assert(!people[1].active, "expected second person to be inactive")
}

export function testQueryOneWithParameters(): void {
  database := try! createPeopleDatabase()
  try! insertPerson(database, "Ada", 99, true)
  try! insertPerson(database, "Grace", 95, false)

  statement := try! prepare(database, "SELECT id, name, score, active FROM people WHERE name = ?")
  row := try! queryOne(statement, ["Grace"])

  assert(row != null, "expected queryOne row")
  person := readPerson(row!)
  assert(person.name == "Grace", "expected queryOne name")
  assert(person.score == 95, "expected queryOne score")
}

export function testExecuteRejectsReturningRows(): void {
  database := try! createPeopleDatabase()
  statement := try! prepare(database, "SELECT 1 AS value")
  result := execute(statement)

  case result {
    _: Success -> assert(false, "expected execute to reject row-producing statements")
    f: Failure -> assert(f.error.message.contains("unexpectedly produced a row"), "expected execute row error")
  }
}

export function testBlobParametersAndRows(): void {
  database := try! open(":memory:")
  try! executeSql(database, `CREATE TABLE files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    payload BLOB NOT NULL
  )`)

  payload: readonly byte[] := [0, 1, 2, 127, 128, 255]
  insertStatement := try! prepare(database, "INSERT INTO files(payload) VALUES (?)")
  insertResult := try! execute(insertStatement, [payload])
  assert(insertResult.changes == 1, "expected blob insert to affect one row")

  selectStatement := try! prepare(database, "SELECT id, payload FROM files WHERE id = ?")
  row := try! queryOne(selectStatement, [insertResult.lastInsertRowId])

  assert(row != null, "expected blob row")
  assertBlobValue(columnValue(row!, "payload"), payload)

  record := try! BlobRecord.fromJsonValue(toJsonRow(row!), true)
  assert(record.id == 1, "expected blob row id")
  assertBytes(record.payload, payload)
}

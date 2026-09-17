MINIREL
=======

Minirel - A simple RDBMS

A simplified single-user relational database system, called MINIREL. 
The MINIREL project involves writing code for both the logical layer and 
the physical layer of a Database Management System.

Done as part of course work for Database Management Systems (E0261) (2014 August Session)

For complete specifications see doc/minirel-doc.pdf

Running on Windows
------------------

MINIREL is an older Unix-oriented C project. The included PowerShell build
script supports Windows with GCC:

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact
npm install
npm run build:minirel
Get-Content query\smoke.query | .\run\minirel.exe
```

Run `.\run\minirel.exe` without redirected input for the interactive
`query >` prompt. Every command must end with a semicolon.

Fields support optional `unique`, `not null`, and `primary key` characteristics
after the type:

```text
create students (
    id = i primary key,
    email = s50 unique,
    name = s50 not null
);
```

Constraints are stored in the database catalog and remain active after the
database is closed and reopened. A relation can have one primary key, and a
primary key implies both `unique` and `not null`. Multiple fields can be marked
`unique`; each one is enforced independently.

MINIREL currently requires every attribute to be present in an insert and does
not have a `NULL` value representation. As a result, `not null` records schema
intent and matches the existing insertion behavior. Nullable values and
defaults require a future record-format extension.

Catalog metadata
----------------

Your recollection about the metadata was correct: MINIREL stores relation and
attribute definitions as fixed-width binary records in the `relcat` and
`attrcat` system tables.

The binary format is now described in one place:

- `include/catalog.h` names every record size and field offset and defines
  readable `RelCatalogRecord`, `AttrCatalogRecord`, and schema types.
- `physical/catalog.c` contains the declarative `RELCAT_SCHEMA` and
  `ATTRCAT_SCHEMA` definitions plus all encoding and decoding.
- `physical/createcats.c` builds the system catalogs from those definitions
  instead of assembling an opaque byte blob by hand.

The on-disk bytes remain compatible with databases created by the original
implementation. New attribute characteristics should be represented in the
catalog types and serialization helpers rather than scattering numeric offsets
through the database code.

Concurrency control
-------------------

MINIREL uses two layers of cross-process locking:

- Relations use shared locks for reads and exclusive locks for inserts,
  deletes, loads, schema changes, and generated result relations.
- The `relcat` and `attrcat` catalogs use a separate lock. Catalog readers
  share it, while metadata and relation-count updates serialize through an
  exclusive lock and refresh the on-disk catalogs before writing.

Locks are implemented with `LockFileEx` on Windows and `fcntl` record locks on
POSIX systems. They are held until the relation is closed, normally by
`closedb` or `quit`. Lock files named `.minirel-*.lock` remain in the database
directory; the operating system releases their locks if a process exits or
crashes.

This prevents cached-page lost updates and makes `unique` and `primary key`
checks safe between cooperating MINIREL processes. Explicit transactions also
use write-ahead logging for process-crash recovery.

Transactions
------------

MINIREL supports one serializable transaction per process:

```text
opendb school;
begin;
insert into students (id = 1, name = "Ada");
insert into courses (id = 10, title = "Databases");
commit;
closedb;
```

Use `rollback;` instead of `commit;` to restore the files as they existed
before the transaction's first write. Transactions use strict two-phase
locking:

- Shared and exclusive table locks are retained through commit or rollback.
- Catalog write locks are retained through transaction completion.
- Before-images of modified table and catalog files support explicit rollback,
  including tables created or dropped in the transaction.
- A reader waits while another transaction has uncommitted writes to its table.
- Any command error marks the transaction failed. A later `commit;` rolls the
  entire transaction back instead of preserving commands that ran before the
  error.
- Lock waits time out after five seconds. The affected transaction is marked
  failed, and a subsequent `commit;` rolls it back.

`closedb;` is rejected while a transaction is active. `quit;` rolls an active
transaction back before closing the database.

This provides serializable isolation for cooperating MINIREL processes at
table granularity.

Write-ahead logging and recovery
--------------------------------

Explicit transactions are protected by a database-local physical write-ahead
log:

```text
school\.minirel.wal
```

The implementation is in `physical/wal.c`, with its interface in
`include/wal.h`. Each binary record has a magic value, format version, record
length, checksum, monotonically increasing LSN, transaction ID, previous LSN,
record type, relation/page identity, and payload.

The first format uses complete 512-byte page images:

- `BEGIN` starts a transaction.
- `PAGE` stores both the page before-image and after-image.
- `BACKUP` records the durable fallback file used for catalog/table create and
  drop recovery.
- `DROP` records that a committed transaction's final state excludes a table,
  even if earlier page records for that table also exist.
- `COMMIT` and `ABORT` establish the transaction outcome.

Before `FlushPage()` may write a dirty table or catalog page, its WAL record is
appended and forced with `_commit()` on Windows or `fsync()` on POSIX. A commit
does not report success until its commit record has also been forced.

`opendb` coordinates recovery through `.minirel-database.lock`. When no live
database session exists, the opener:

1. Validates WAL record lengths, versions, and checksums.
2. Truncates an incomplete or torn final record.
3. Redoes committed page after-images.
4. Undoes incomplete transactions in reverse order.
5. Restores or removes files involved in incomplete create/drop operations.
6. Removes completed backup artifacts.
7. Forces recovered files and truncates the recovered WAL as a clean
   checkpoint.

Recovery is idempotent: if MINIREL crashes during recovery, the next opener can
repeat it. Deterministic tests terminate MINIREL after WAL force, after a data
page write, after durable commit, and during recovery.

Current WAL scope and limitations:

- WAL protection applies to explicit `begin`/`commit` transactions, including
  MCP `execute_transaction`.
- Commands run outside a transaction are not yet converted into implicit
  autocommit transactions.
- Whole-file before-image backups remain as a schema/catalog recovery safety
  net during this first WAL version.
- Recovery handles process termination and torn WAL tails. Strong guarantees
  across storage-device failure still depend on the operating system and
  hardware honoring `_commit()`/`fsync()`.
- WAL is checkpointed when a database opener obtains exclusive recovery access;
  there is not yet a concurrent background checkpoint or WAL-size policy.

Row updates
-----------

Update one or more fixed-length records with:

```text
update students set name = "Grace Hopper", score = 99.5
where (id = 2 and score >= 80);
```

Assignments are comma-separated and predicates may be joined with `and`.
Comparison operators are `=`, `>=`, `>`, `<=`, `<>`, and `<`. The command:

- Takes an exclusive table lock.
- Preserves the existing record length and page layout.
- Validates column names, data types, string lengths, complete-record
  duplication, `UNIQUE`, and `PRIMARY KEY`.
- Computes and validates the complete final table state before writing, so a
  multi-row constraint failure cannot leave a partially updated command.
- Participates in explicit `commit` and `rollback` transactions.

No additional MCP tool is needed. Use `update` inside `run_minirel_commands` or,
preferably, `execute_transaction`.

MCP server
----------

The stdio MCP server deliberately exposes five broad tools rather than one tool
per MINIREL command:

- `minirel_status` to check whether the database executable is built.
- `run_minirel_commands` to run an ordered set of administrative, read, or
  advanced commands.
- `discover_databases` to find databases and summarize their user tables and
  record counts.
- `discover_tables` to return table schemas, column types and constraints,
  record/page counts, and storage details for one database.
- `execute_transaction` to wrap a set of commands with `opendb`, `begin`,
  `commit`, and `closedb`.

The server also exposes a `minirel_help` MCP prompt. It lists the supported
syntax, known unimplemented commands, examples using school, students, courses,
and a library, and the correct way to compose a transaction.

For example, create a database and its initial tables with
`run_minirel_commands`:

```json
{
  "workingDirectory": "C:\\databases",
  "commands": [
    "createdb school",
    "opendb school",
    "create students (id = i primary key, name = s50 not null)",
    "create courses (id = i primary key, title = s40 not null)",
    "closedb"
  ]
}
```

Use `execute_transaction` for related changes that must either all commit or all
roll back:

```json
{
  "database": "school",
  "workingDirectory": "C:\\databases",
  "commands": [
    "insert into students (id = 1, name = \"Ada\")",
    "insert into courses (id = 10, title = \"Database Systems\")"
  ]
}
```

Do not include `opendb`, `closedb`, `begin`, `commit`, `rollback`, or `quit` in
an `execute_transaction` call; the tool owns that lifecycle. A transaction with
any MINIREL error is rolled back.

Start it with:

```powershell
npm run mcp
```

Example MCP client configuration:

```json
{
  "mcpServers": {
    "minirel": {
      "command": "node",
      "args": ["<repo-root>\\mcp\\server.mjs"],
      "env": {
        "MINIREL_BIN": "<repo-root>\\run\\minirel.exe"
      }
    }
  }
}
```

Both execution tools append `quit;` when it is absent, so each call runs in a
fresh MINIREL process. Every command is supplied as a separate array entry,
which matches MINIREL's one-command-per-line parser.

### VS Code

Open this repository as the VS Code workspace. The checked-in
`.vscode/mcp.json` configuration lets VS Code start the stdio server
automatically:

1. Run `npm install` and `npm run build:minirel`.
2. Open the repository with `code .`.
3. Run **MCP: List Servers** from the Command Palette and start `minirel`.
4. Confirm that you trust the local server when prompted.
5. In Chat agent mode, enable the `minirel` tools from **Configure Tools**.
   Use discovery before composing commands, and prefer `execute_transaction`
   for related changes inside an existing database.

Do not separately keep `npm run mcp` running when using VS Code. VS Code owns
the server's stdin/stdout connection and launches the process itself.

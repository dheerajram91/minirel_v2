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

Fields can optionally enforce unique values by adding `unique` after the type:

```text
create students (id = i unique, name = s50);
```

The constraint is stored in the database catalog and remains active after the
database is closed and reopened. Multiple fields can be marked `unique`; each
one is enforced independently.

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

MCP server
----------

The stdio MCP server exposes:

- `minirel_status` to check whether the database executable is built.
- `run_minirel_script` to execute one or more MINIREL commands.

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

`run_minirel_script` appends `quit;` when it is absent, so each call runs in a
fresh MINIREL process. Include `opendb <path>;` in a script when working with an
existing database.

### VS Code

Open this repository as the VS Code workspace. The checked-in
`.vscode/mcp.json` configuration lets VS Code start the stdio server
automatically:

1. Run `npm install` and `npm run build:minirel`.
2. Open the repository with `code .`.
3. Run **MCP: List Servers** from the Command Palette and start `minirel`.
4. Confirm that you trust the local server when prompted.
5. In Chat agent mode, enable `minirel_status` or `run_minirel_script` from
   **Configure Tools**.

Do not separately keep `npm run mcp` running when using VS Code. VS Code owns
the server's stdin/stdout connection and launches the process itself.

import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, stat } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { Client } from "@modelcontextprotocol/client";
import { StdioClientTransport } from "@modelcontextprotocol/client/stdio";

function textContent(result) {
  return result.content
    .filter((item) => item.type === "text")
    .map((item) => item.text)
    .join("\n");
}

test("consolidated MCP tools discover and transact with MINIREL", async () => {
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-mcp-"));
  const rootMappedDatabase = "mcprootmapping";
  const rootMappedPath = path.resolve("DB", rootMappedDatabase);
  const client = new Client(
    { name: "minirel-test", version: "1.0.0" },
    { versionNegotiation: { mode: "auto" } },
  );
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: ["mcp/server.mjs"],
    cwd: path.resolve("."),
    stderr: "pipe",
  });

  try {
    await client.connect(transport);
    const { tools } = await client.listTools();
    assert.deepEqual(
      tools.map((tool) => tool.name),
      [
        "minirel_status",
        "run_minirel_commands",
        "discover_databases",
        "discover_tables",
        "execute_transaction",
      ],
    );

    const { prompts } = await client.listPrompts();
    assert.deepEqual(
      prompts.map((prompt) => prompt.name),
      ["minirel_help"],
    );
    const help = await client.getPrompt({
      name: "minirel_help",
      arguments: {},
    });
    assert.match(help.messages[0].content.text, /createdb school;/);
    assert.match(help.messages[0].content.text, /begin;/);
    assert.match(help.messages[0].content.text, /create library/);

    await mkdir(path.join(workingDirectory, "not-a-database"));
    const emptyDiscovery = await client.callTool({
      name: "discover_databases",
      arguments: { workingDirectory },
    });
    assert.equal(Boolean(emptyDiscovery.isError), false);
    assert.deepEqual(emptyDiscovery.structuredContent.databases, []);

    const create = await client.callTool({
      name: "run_minirel_commands",
      arguments: {
        workingDirectory,
        commands: [
          "createdb school",
          "opendb school",
          "create students (id = i primary key, name = s24 not null)",
          "create courses (id = i primary key, title = s40 not null)",
          "closedb",
        ],
      },
    });
    assert.equal(Boolean(create.isError), false);

    const insert = await client.callTool({
      name: "execute_transaction",
      arguments: {
        database: "school",
        workingDirectory,
        commands: [
          'insert into students (id = 1, name = "Ada")',
          'insert into students (id = 2, name = "Grace")',
          'insert into courses (id = 10, title = "Database Systems")',
        ],
      },
    });
    assert.equal(Boolean(insert.isError), false);

    const update = await client.callTool({
      name: "execute_transaction",
      arguments: {
        database: "school",
        workingDirectory,
        commands: ['update students set name = "Grace Hopper" where (id = 2)'],
      },
    });
    assert.equal(Boolean(update.isError), false);

    const databases = await client.callTool({
      name: "discover_databases",
      arguments: { workingDirectory },
    });
    assert.equal(Boolean(databases.isError), false);
    assert.equal(databases.structuredContent.databaseCount, 1);
    assert.deepEqual(databases.structuredContent.databases[0].tables, [
      { name: "courses", recordCount: 1 },
      { name: "students", recordCount: 2 },
    ]);

    const tables = await client.callTool({
      name: "discover_tables",
      arguments: { database: "school", workingDirectory },
    });
    assert.equal(Boolean(tables.isError), false);
    assert.equal(tables.structuredContent.tableCount, 2);
    const students = tables.structuredContent.tables.find(
      (table) => table.name === "students",
    );
    assert.equal(students.recordCount, 2);
    assert.deepEqual(students.columns, [
      {
        offset: 0,
        length: 4,
        type: "integer",
        name: "id",
        unique: true,
        notNull: true,
        primaryKey: true,
        nullable: false,
      },
      {
        offset: 4,
        length: 24,
        type: "string",
        name: "name",
        unique: false,
        notNull: true,
        primaryKey: false,
        nullable: false,
      },
    ]);
    assert.equal(students.recordFormat, "null-bitmap-v1");
    assert.equal(students.nullBitmapBytes, 1);
    assert.equal(students.dataLength, 28);

    const failedTransaction = await client.callTool({
      name: "execute_transaction",
      arguments: {
        database: "school",
        workingDirectory,
        commands: [
          'insert into students (id = 3, name = "Linus")',
          'insert into students (id = 1, name = "Duplicate")',
        ],
      },
    });
    assert.equal(Boolean(failedTransaction.isError), true);
    assert.match(textContent(failedTransaction), /<ERROR 139>/);
    assert.match(textContent(failedTransaction), /<ERROR 144>/);

    const read = await client.callTool({
      name: "run_minirel_commands",
      arguments: {
        workingDirectory,
        commands: ["opendb school", "print students", "closedb"],
      },
    });
    assert.equal(Boolean(read.isError), false);
    assert.match(textContent(read), /Ada/);
    assert.match(textContent(read), /Grace Hopper/);
    assert.doesNotMatch(textContent(read), /Linus/);

    const invalidTransaction = await client.callTool({
      name: "execute_transaction",
      arguments: {
        database: "school",
        workingDirectory,
        commands: ["begin"],
      },
    });
    assert.equal(Boolean(invalidTransaction.isError), true);
    assert.match(textContent(invalidTransaction), /controls a database or transaction/);

    const destroy = await client.callTool({
      name: "run_minirel_commands",
      arguments: {
        workingDirectory,
        commands: ["destroydb school"],
      },
    });
    assert.equal(Boolean(destroy.isError), false);

    const finalDiscovery = await client.callTool({
      name: "discover_databases",
      arguments: { workingDirectory },
    });
    assert.deepEqual(finalDiscovery.structuredContent.databases, []);

    const invalidDiscovery = await client.callTool({
      name: "discover_databases",
      arguments: { workingDirectory: path.join(workingDirectory, "missing") },
    });
    assert.equal(Boolean(invalidDiscovery.isError), true);
    assert.match(textContent(invalidDiscovery), /ENOENT/);
    const rootMappedCreate = await client.callTool({
      name: "run_minirel_commands",
      arguments: {
        workingDirectory: path.resolve("."),
        commands: [`createdb ${rootMappedDatabase}`],
      },
    });
    assert.equal(Boolean(rootMappedCreate.isError), false);
    assert.equal(
      (
        await stat(
          path.resolve("DB", rootMappedDatabase, "relcat"),
        )
      ).isFile(),
      true,
    );
    const rootMappedDestroy = await client.callTool({
      name: "run_minirel_commands",
      arguments: {
        workingDirectory: path.resolve("."),
        commands: [`destroydb ${rootMappedDatabase}`],
      },
    });
    assert.equal(Boolean(rootMappedDestroy.isError), false);
  } finally {
    await client.close();
    await rm(workingDirectory, { recursive: true, force: true });
    await rm(rootMappedPath, { recursive: true, force: true });
  }
});

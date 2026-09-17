import assert from "node:assert/strict";
import test from "node:test";

import {
  buildCreateDatabaseCommand,
  buildCreateRelationCommand,
  buildInsertCommands,
  buildOpenDatabaseScript,
  formatValue,
} from "../mcp/commands.mjs";
import {
  buildTransactionScript,
  normalizeCommands,
} from "../mcp/transactions.mjs";

test("structured commands generate valid MINIREL syntax", () => {
  assert.equal(buildCreateDatabaseCommand("school"), "createdb school;");
  assert.equal(
    buildCreateRelationCommand("students", [
      { name: "id", type: "integer", primaryKey: true },
      { name: "email", type: "string", length: 50, unique: true },
      { name: "name", type: "string", length: 24, notNull: true },
    ]),
    "create students (id = i primary key, email = s50 unique, name = s24 not null);",
  );
  assert.deepEqual(
    buildInsertCommands("students", [
      { id: 1, name: "Ada" },
      { id: 2, name: "Grace" },
    ]),
    [
      'insert into students (id = 1, name = "Ada");',
      'insert into students (id = 2, name = "Grace");',
    ],
  );
  assert.equal(
    buildOpenDatabaseScript("school", ["print students;"]),
    "opendb school;\nprint students;\nclosedb;",
  );
});

test("structured commands reject unsafe or invalid inputs", () => {
  assert.throws(
    () => buildCreateDatabaseCommand("school; destroydb other"),
    /Database name must/,
  );
  assert.throws(
    () =>
      buildCreateRelationCommand("students", [
        { name: "id", type: "integer", primaryKey: true },
        { name: "otherId", type: "integer", primaryKey: true },
      ]),
    /only one primary key/,
  );
  assert.throws(
    () =>
      buildCreateRelationCommand("students", [
        { name: "name", type: "string", length: 51 },
      ]),
    /length from 1 to 50/,
  );
  assert.throws(() => formatValue('Ada"; destroydb school;'), /cannot contain/);
});

test("command sets and transactions are composed safely", () => {
  assert.deepEqual(normalizeCommands(["print students", "closedb;"]), [
    "print students;",
    "closedb;",
  ]);
  assert.equal(
    buildTransactionScript("school", [
      'insert into students (id = 1, name = "Ada")',
      'insert into courses (id = 10, title = "Databases");',
    ]),
    [
      "opendb school;",
      "begin;",
      'insert into students (id = 1, name = "Ada");',
      'insert into courses (id = 10, title = "Databases");',
      "commit;",
      "closedb;",
    ].join("\n"),
  );
  assert.throws(
    () => buildTransactionScript("school", ["commit;"]),
    /controls a database or transaction/,
  );
  assert.throws(
    () => normalizeCommands(["print students; destroy students;"]),
    /exactly one command/,
  );
});

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import {
  getMinirelStatus,
  normalizeScript,
  runMinirelScript,
} from "../mcp/minirel-runner.mjs";

test("normalizeScript appends a missing semicolon and quit command", () => {
  assert.equal(normalizeScript("print students"), "print students;\nquit;\n");
});

test("normalizeScript preserves an existing quit command", () => {
  assert.equal(normalizeScript("createdb test;\nquit;"), "createdb test;\nquit;\n");
});

test("normalizeScript rejects empty scripts", () => {
  assert.throws(() => normalizeScript("  "), /cannot be empty/);
});

test("catalog refactor preserves the existing binary format", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  if (os.endianness() !== "LE") {
    context.skip("The legacy catalog format is native-endian.");
    return;
  }

  const expectedHashes = {
    relcat: "15ab3a14f469c41b56b5b554100633b5d300dcbe4a72a7f8201354076b30e99c",
    attrcat: "aa4a97a80a76f8e48b610f232ce6c3065488e7074fb39d09af745857d54c6791",
  };
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const result = await runMinirelScript({
      workingDirectory,
      script: "createdb catalogtest;",
    });
    assert.deepEqual(result.errors, []);

    for (const [name, expectedHash] of Object.entries(expectedHashes)) {
      const contents = await readFile(
        path.join(workingDirectory, "catalogtest", name),
      );
      const actualHash = createHash("sha256").update(contents).digest("hex");
      assert.equal(actualHash, expectedHash);
    }
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("runMinirelScript executes a database lifecycle", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }

  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb testdb;",
        "opendb testdb;",
        "create people (name = s24, id = i);",
        'insert into people (name = "Grace", id = 7);',
        "print people;",
        "closedb;",
        "destroydb testdb;",
      ].join("\n"),
    });

    assert.match(result.stdout, /Grace/);
    assert.match(result.stdout, /Goodbye from MINIREL/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("non-unique fields can repeat values", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }

  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i, name = s50);",
        'insert into students (id = 1, name = "Ada");',
        'insert into students (id = 1, name = "Grace");',
        "print students;",
        "closedb;",
        "destroydb school;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    assert.match(result.stdout, /Ada/);
    assert.match(result.stdout, /Grace/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("unique fields reject duplicate values after reopening", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }

  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const setup = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i unique, name = s50 unique);",
        'insert into students (id = 1, name = "Ada");',
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(setup.errors, []);

    const duplicate = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        'insert into students (id = 2, name = "Grace");',
        'insert into students (id = 1, name = "Linus");',
        'insert into students (id = 2, name = "Ada");',
        "print students;",
        "closedb;",
        "destroydb school;",
      ].join("\n"),
    });

    assert.deepEqual(duplicate.errors, [
      {
        code: 137,
        message:
          "Unique constraint violated! The value already exists for a unique attribute.",
      },
      {
        code: 137,
        message:
          "Unique constraint violated! The value already exists for a unique attribute.",
      },
    ]);
    assert.match(duplicate.stdout, /Ada/);
    assert.match(duplicate.stdout, /Grace/);
    assert.doesNotMatch(duplicate.stdout, /\|\s+1\s+\|\s+Linus\s+\|/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("primary keys imply uniqueness and persist after reopening", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }

  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const setup = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, name = s50 not null);",
        'insert into students (id = 1, name = "Ada");',
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(setup.errors, []);

    const duplicate = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        'insert into students (id = 2, name = "Grace");',
        'insert into students (id = 1, name = "Linus");',
        "print students;",
        "closedb;",
        "destroydb school;",
      ].join("\n"),
    });

    assert.deepEqual(duplicate.errors, [
      {
        code: 139,
        message: "Primary key constraint violated! The key value already exists.",
      },
    ]);
    assert.match(duplicate.stdout, /\|\s+1\s+\|\s+Ada\s+\|/);
    assert.match(duplicate.stdout, /\|\s+2\s+\|\s+Grace\s+\|/);
    assert.doesNotMatch(duplicate.stdout, /\|\s+1\s+\|\s+Linus\s+\|/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("relations reject multiple primary keys", async (context) => {
  const status = await getMinirelStatus();
  if (!status.available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }

  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-"));
  try {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, email = s50 primary key);",
        'insert into students (id = 1, email = "ada@example.com");',
        "closedb;",
        "destroydb school;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, [
      {
        code: 138,
        message: "A relation can declare only one primary key.",
      },
      {
        code: 101,
        message: "Relation does not exist! Please check the name and try again.",
      },
    ]);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { inspectMinirelDatabase } from "../mcp/databases.mjs";
import { runMinirelScript } from "../mcp/minirel-runner.mjs";

async function withDatabase(run) {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-null-"),
  );
  try {
    await run(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
}

test("nullable values and omitted attributes persist across reopen", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, name = s12 not null, email = s20 unique, score = f);",
        'insert into students (id = 1, name = "Ada");',
        'insert into students (id = 2, name = "Grace", email = null);',
        'insert into students (id = 3, name = "Linus", email = null, score = 91.5);',
        "closedb;",
        "opendb school;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    assert.match(result.stdout, /\|\s+1\s+\|\s+Ada\s+\|\s+NULL\s+\|\s+NULL\s+\|/);
    assert.match(result.stdout, /\|\s+2\s+\|\s+Grace\s+\|\s+NULL\s+\|\s+NULL\s+\|/);
    assert.match(result.stdout, /\|\s+3\s+\|\s+Linus\s+\|\s+NULL\s+\|\s+91\.5\s+\|/);

    const inspected = await inspectMinirelDatabase({
      database: "school",
      workingDirectory,
    });
    const students = inspected.tables.find(({ name }) => name === "students");
    assert.equal(students.recordFormat, "null-bitmap-v1");
    assert.equal(students.dataLength, 40);
    assert.equal(students.nullBitmapBytes, 1);
    assert.equal(
      students.columns.find(({ name }) => name === "email").nullable,
      true,
    );
  });
});

test("strict inserts reject invalid and required values without partial rows", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, name = s5 not null, score = f);",
        "insert into students (id = 1);",
        'insert into students (id = 2, name = null);',
        'insert into students (id = "12x", name = "Bad");',
        'insert into students (id = 999999999999, name = "Huge");',
        'insert into students (id = 5, name = "TooLong");',
        'insert into students (id = 6, name = "Fine", name = "Again");',
        'insert into students (id = 7, unknown = "Value");',
        'insert into students (id = 8, name = "Valid");',
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(
      result.errors.map(({ code }) => code),
      [147, 147, 127, 148, 129, 136, 123],
    );
    assert.match(result.stdout, /\|\s+8\s+\|\s+Valid\s+\|\s+NULL\s+\|/);
    assert.doesNotMatch(result.stdout, /\|\s+[1-7]\s+\|/);
  });
});

test("strict schemas reject duplicate attributes and malformed types", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create duplicateattrs (id = i, id = i);",
        "create zerostring (name = s0);",
        "create longstring (name = s51);",
        "create malformed (id = i2);",
        "print relcat;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(
      result.errors.map(({ code }) => code),
      [136, 106, 129, 106],
    );
    const catalog = result.stdout.split("print relcat;").at(-1);
    assert.doesNotMatch(
      catalog,
      /duplicateattrs|zerostring|longstring|malformed/,
    );
  });
});

test("unique columns allow multiple NULLs but reject duplicate values", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, email = s20 unique);",
        "insert into students (id = 1);",
        "insert into students (id = 2, email = null);",
        'insert into students (id = 3, email = "same@school");',
        'insert into students (id = 4, email = "same@school");',
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors.map(({ code }) => code), [137]);
    assert.match(result.stdout, /\|\s+1\s+\|\s+NULL\s+\|/);
    assert.match(result.stdout, /\|\s+2\s+\|\s+NULL\s+\|/);
    assert.match(result.stdout, /\|\s+3\s+\|\s+same@school\s+\|/);
    assert.doesNotMatch(result.stdout, /\|\s+4\s+\|/);
  });
});

test("NULL predicates, updates, projection, and deletion share one representation", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, email = s20, score = f);",
        'insert into students (id = 1, email = "ada", score = 10);',
        "insert into students (id = 2);",
        "insert into students (id = 3, email = null);",
        "update students set score = null where (id = 1);",
        "select into missing from students where (email = null);",
        "project into contacts from students (id, email);",
        "print missing;",
        "print contacts;",
        "delete from students where (email = null);",
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    const missing = result.stdout.split("print missing;").at(-1).split("print contacts;")[0];
    assert.match(missing, /\|\s+2\s+\|\s+NULL\s+\|\s+NULL\s+\|/);
    assert.match(missing, /\|\s+3\s+\|\s+NULL\s+\|\s+NULL\s+\|/);

    const contacts = result.stdout.split("print contacts;").at(-1).split("delete from")[0];
    assert.match(contacts, /\|\s+1\s+\|\s+ada\s+\|/);
    assert.match(contacts, /\|\s+2\s+\|\s+NULL\s+\|/);
    assert.match(contacts, /\|\s+3\s+\|\s+NULL\s+\|/);

    const remaining = result.stdout.split("print students;").at(-1);
    assert.match(remaining, /\|\s+1\s+\|\s+ada\s+\|\s+NULL\s+\|/);
    assert.doesNotMatch(remaining, /\|\s+[23]\s+\|/);
  });
});

test("joins do not match NULL keys and preserve nullable payloads", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create lefts (id = i primary key, code = i, note = s8);",
        "create rights (rid = i primary key, code = i, detail = s8);",
        'insert into lefts (id = 1, code = null, note = "leftnull");',
        'insert into lefts (id = 2, code = 7);',
        'insert into rights (rid = 10, code = null, detail = "rightnul");',
        "insert into rights (rid = 20, code = 7);",
        "join into matches (lefts.code, rights.code);",
        "print matches;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    const matches = result.stdout.split("print matches;").at(-1);
    assert.match(matches, /\|\s+2\s+\|\s+7\s+\|\s+NULL\s+\|\s+20\s+\|\s+NULL\s+\|/);
    assert.doesNotMatch(matches, /leftnull|rightnul/);
    assert.doesNotMatch(matches, /\|\s+1\s+\||\|\s+10\s+\|/);
  });
});

test("NULL updates participate in rollback and durable commit", async () => {
  await withDatabase(async (workingDirectory) => {
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, email = s20);",
        'insert into students (id = 1, email = "ada@school");',
        "begin;",
        "update students set email = null where (id = 1);",
        "rollback;",
        "print students;",
        "begin;",
        "update students set email = null where (id = 1);",
        "commit;",
        "closedb;",
        "opendb school;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    const afterRollback = result.stdout
      .split("print students;")[1]
      .split("begin;")[0];
    assert.match(afterRollback, /ada@school/);
    const afterCommit = result.stdout.split("print students;").at(-1);
    assert.match(afterCommit, /\|\s+1\s+\|\s+NULL\s+\|/);
  });
});

test("load rejects truncated and duplicate binary records transactionally", async () => {
  await withDatabase(async (workingDirectory) => {
    const setup = await runMinirelScript({
      workingDirectory,
      script: [
        "createdb school;",
        "opendb school;",
        "create students (id = i primary key, name = s5);",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(setup.errors, []);

    const truncatedPath = path.join(workingDirectory, "school", "truncated");
    const duplicatePath = path.join(workingDirectory, "school", "duplicate");
    await writeFile(truncatedPath, Buffer.from([1, 2, 3]));

    const record = Buffer.alloc(10);
    record.writeInt32LE(1, 0);
    record.write("Ada", 4, "ascii");
    await writeFile(duplicatePath, Buffer.concat([record, record]));

    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "load students from truncated;",
        "begin;",
        "load students from duplicate;",
        "commit;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors.map(({ code }) => code), [133, 139, 144]);
    const table = result.stdout.split("print students;").at(-1);
    assert.doesNotMatch(table, /\|\s+1\s+\|/);
  });
});

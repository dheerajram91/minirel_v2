import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { runMinirelScript } from "../mcp/minirel-runner.mjs";

async function createSchool(workingDirectory) {
  const result = await runMinirelScript({
    workingDirectory,
    script: [
      "createdb school;",
      "opendb school;",
      "create students (id = i primary key, email = s30 unique, name = s24 not null, score = f);",
      'insert into students (id = 1, email = "ada@school", name = "Ada", score = 90);',
      'insert into students (id = 2, email = "grace@school", name = "Grace", score = 80);',
      'insert into students (id = 3, email = "linus@school", name = "Linus", score = 80);',
      "closedb;",
    ].join("\n"),
  });
  assert.deepEqual(result.errors, []);
}

test("update changes matching rows with multiple assignments and predicates", async () => {
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-update-"));
  try {
    await createSchool(workingDirectory);
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        'update students set name = "Grace Hopper", score = 99.5 where (id = 2 and score >= 80);',
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, []);
    assert.match(result.stdout, /Updated 1 record\(s\)/);
    assert.match(result.stdout, /Grace Hopper/);
    assert.match(result.stdout, /99\.5/);
    assert.match(result.stdout, /Linus/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("multi-row update validates the complete final state before writing", async () => {
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-update-"));
  try {
    await createSchool(workingDirectory);
    const result = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        'update students set email = "shared@school" where (score >= 80);',
        "print students;",
        "closedb;",
      ].join("\n"),
    });

    assert.deepEqual(result.errors, [
      {
        code: 137,
        message:
          "Unique constraint violated! The value already exists for a unique attribute.",
      },
    ]);
    const printedTable = result.stdout.split("print students;").at(-1);
    assert.doesNotMatch(printedTable, /shared@school/);
    assert.match(printedTable, /ada@school/);
    assert.match(printedTable, /grace@school/);
    assert.match(printedTable, /linus@school/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("update rejects primary-key conflicts and oversized strings", async () => {
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-update-"));
  try {
    await createSchool(workingDirectory);
    const primaryKeyConflict = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "update students set id = 1 where (id = 2);",
        "print students;",
        "closedb;",
      ].join("\n"),
    });
    assert.equal(primaryKeyConflict.errors[0].code, 139);
    const keyTable = primaryKeyConflict.stdout.split("print students;").at(-1);
    assert.match(keyTable, /\|\s+1\s+\|/);
    assert.match(keyTable, /\|\s+2\s+\|/);

    const oversizedString = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        'update students set name = "This name is longer than 24 bytes" where (id = 1);',
        "print students;",
        "closedb;",
      ].join("\n"),
    });
    assert.equal(oversizedString.errors[0].code, 129);
    const nameTable = oversizedString.stdout.split("print students;").at(-1);
    assert.doesNotMatch(nameTable, /This name is longer/);
    assert.match(nameTable, /Ada/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("update participates in explicit commit and rollback", async () => {
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-update-"));
  try {
    await createSchool(workingDirectory);
    const rollback = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "begin;",
        'update students set name = "Temporary" where (id = 1);',
        "rollback;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(rollback.errors, []);
    const rolledBackTable = rollback.stdout.split("print students;").at(-1);
    assert.doesNotMatch(rolledBackTable, /Temporary/);
    assert.match(rolledBackTable, /Ada/);

    const commit = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "begin;",
        'update students set name = "Ada Lovelace" where (id = 1);',
        "commit;",
        "closedb;",
        "opendb school;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(commit.errors, []);
    assert.match(commit.stdout, /Ada Lovelace/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

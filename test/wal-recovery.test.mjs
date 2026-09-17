import assert from "node:assert/strict";
import { appendFile, mkdtemp, readdir, rm, stat } from "node:fs/promises";
import { spawn } from "node:child_process";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import {
  defaultBinaryPath,
  getMinirelStatus,
  normalizeScript,
  runMinirelScript,
} from "../mcp/minirel-runner.mjs";

async function setupSchool(workingDirectory) {
  const result = await runMinirelScript({
    workingDirectory,
    script: [
      "createdb school;",
      "opendb school;",
      "create students (id = i primary key, name = s24);",
      "closedb;",
    ].join("\n"),
  });
  assert.deepEqual(result.errors, []);
}

async function crashMinirel(workingDirectory, script, crashPoint) {
  const child = spawn(defaultBinaryPath(), [], {
    cwd: workingDirectory,
    env: { ...process.env, MINIREL_CRASH_POINT: crashPoint },
    stdio: ["pipe", "pipe", "pipe"],
  });
  let stdout = "";
  let stderr = "";
  child.stdout.setEncoding("utf8");
  child.stderr.setEncoding("utf8");
  child.stdout.on("data", (chunk) => {
    stdout += chunk;
  });
  child.stderr.on("data", (chunk) => {
    stderr += chunk;
  });
  child.stdin.end(normalizeScript(script));
  const exitCode = await new Promise((resolve, reject) => {
    child.once("error", reject);
    child.once("exit", resolve);
  });
  assert.equal(exitCode, 86, `${stdout}\n${stderr}`);
}

async function readStudents(workingDirectory) {
  return runMinirelScript({
    workingDirectory,
    script: "opendb school;\nprint students;\nclosedb;",
  });
}

async function assertRecoveryClean(workingDirectory) {
  const databasePath = path.join(workingDirectory, "school");
  const files = await readdir(databasePath);
  assert.equal(
    files.some((name) => name.startsWith(".minirel-tx-")),
    false,
  );
  assert.equal((await stat(path.join(databasePath, ".minirel.wal"))).size, 0);
}

test("recovery undoes a crash after WAL force but before page write", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        'insert into students (id = 1, name = "Uncommitted");',
        "commit;",
      ].join("\n"),
      "after_wal_page",
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    assert.doesNotMatch(recovered.stdout.split("print students;").at(-1), /Uncommitted/);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery undoes a data page written before commit", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        'insert into students (id = 2, name = "Loser");',
        "commit;",
      ].join("\n"),
      "after_data_page",
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    assert.doesNotMatch(recovered.stdout.split("print students;").at(-1), /Loser/);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery redoes a transaction with a durable commit record", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        'insert into students (id = 3, name = "Durable");',
        "commit;",
      ].join("\n"),
      "after_commit_log",
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    assert.match(recovered.stdout, /Durable/);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery is repeatable after crashing during redo", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    const committed = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "begin;",
        'insert into students (id = 4, name = "Repeatable");',
        "commit;",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(committed.errors, []);

    await crashMinirel(
      workingDirectory,
      "opendb school;",
      "after_recovery_page",
    );
    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    assert.match(recovered.stdout, /Repeatable/);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery truncates a torn WAL tail", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    const committed = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "begin;",
        'insert into students (id = 5, name = "TornTail");',
        "commit;",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(committed.errors, []);
    await appendFile(
      path.join(workingDirectory, "school", ".minirel.wal"),
      Buffer.from([0x4c, 0x57, 0x52, 0x4d, 0x01]),
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    assert.match(recovered.stdout, /TornTail/);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery removes a table from an incomplete create", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        "create courses (id = i primary key, title = s24);",
        "commit;",
      ].join("\n"),
      "after_data_page",
    );

    const recovered = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint courses;\nclosedb;",
    });
    assert.equal(recovered.errors[0].code, 101);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery restores a table dropped before commit", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        "destroy students;",
        "commit;",
      ].join("\n"),
      "after_data_page",
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("recovery preserves a committed table drop", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        'update students set name = "Changed" where (id = 999);',
        "destroy students;",
        "commit;",
      ].join("\n"),
      "after_commit_log",
    );

    const recovered = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint students;\nclosedb;",
    });
    assert.equal(recovered.errors[0].code, 101);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("resource recovery remains repeatable after a second crash", async (context) => {
  if (!(await getMinirelStatus()).available) {
    context.skip("MINIREL executable has not been built.");
    return;
  }
  const workingDirectory = await mkdtemp(path.join(os.tmpdir(), "minirel-wal-"));
  try {
    await setupSchool(workingDirectory);
    await crashMinirel(
      workingDirectory,
      [
        "opendb school;",
        "begin;",
        "destroy students;",
        "commit;",
      ].join("\n"),
      "after_data_page",
    );
    await crashMinirel(
      workingDirectory,
      "opendb school;",
      "after_recovery_resource",
    );

    const recovered = await readStudents(workingDirectory);
    assert.deepEqual(recovered.errors, []);
    await assertRecoveryClean(workingDirectory);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

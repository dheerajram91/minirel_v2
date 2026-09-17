import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp, readdir, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import {
  defaultBinaryPath,
  runMinirelScript,
} from "../mcp/minirel-runner.mjs";

function startSession(workingDirectory) {
  const child = spawn(defaultBinaryPath(), [], {
    cwd: workingDirectory,
    stdio: ["pipe", "pipe", "pipe"],
  });
  let output = "";
  let errorOutput = "";
  child.stdout.on("data", (chunk) => {
    output += chunk;
  });
  child.stderr.on("data", (chunk) => {
    errorOutput += chunk;
  });
  return {
    child,
    output: () => output,
    errorOutput: () => errorOutput,
  };
}

async function waitForExit(session, timeoutMs = 12_000) {
  if (session.child.exitCode !== null) {
    return;
  }
  await Promise.race([
    new Promise((resolve, reject) => {
      session.child.once("error", reject);
      session.child.once("exit", resolve);
    }),
    new Promise((_, reject) =>
      setTimeout(
        () =>
          reject(
            new Error(
              `MINIREL did not exit.\n${session.output()}\n${
                session.errorOutput()
              }`,
            ),
          ),
        timeoutMs,
      ),
    ),
  ]);
}

async function setupSchool(workingDirectory, extraCommands = []) {
  const setup = await runMinirelScript({
    workingDirectory,
    script: [
      "createdb school;",
      "opendb school;",
      "create students (id = i primary key, name = s24);",
      ...extraCommands,
      "closedb;",
    ].join("\n"),
  });
  assert.deepEqual(setup.errors, []);
}

test("commit persists changes and rollback restores table contents", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-transaction-"),
  );
  try {
    await setupSchool(workingDirectory);

    const commit = await runMinirelScript({
      workingDirectory,
      script:
        "opendb school;\n" +
        "begin;\n" +
        'insert into students (id = 1, name = "Ada");\n' +
        "commit;\n" +
        "closedb;",
    });
    assert.deepEqual(commit.errors, []);

    const rollback = await runMinirelScript({
      workingDirectory,
      script: [
        "opendb school;",
        "begin;",
        ...Array.from(
          { length: 25 },
          (_, index) =>
            `insert into students (id = ${index + 2}, name = "R${index}");`,
        ),
        "rollback;",
        "print students;",
        "closedb;",
      ].join("\n"),
    });
    assert.deepEqual(rollback.errors, []);
    assert.match(rollback.stdout, /Ada/);
    assert.doesNotMatch(rollback.stdout, /\|\s+2\s+\|/);
    const databaseFiles = await readdir(path.join(workingDirectory, "school"));
    assert.equal(
      databaseFiles.some((fileName) => fileName.startsWith(".minirel-tx-")),
      false,
    );
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("rollback removes created tables and restores dropped tables", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-schema-transaction-"),
  );
  try {
    await setupSchool(workingDirectory);

    const transaction = await runMinirelScript({
      workingDirectory,
      script:
        "opendb school;\n" +
        "begin;\n" +
        "create courses (id = i primary key, title = s24);\n" +
        "print students;\n" +
        "destroy students;\n" +
        "rollback;\n" +
        'insert into students (id = 1, name = "Ada");\n' +
        'insert into courses (id = 10, title = "Databases");\n' +
        "closedb;",
    });

    assert.equal(transaction.errors.length, 1);
    assert.equal(transaction.errors[0].code, 101);

    const read = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint students;\nclosedb;",
    });
    assert.deepEqual(read.errors, []);
    assert.match(read.stdout, /Ada/);
  } finally {
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("uncommitted writes block readers until commit", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-isolation-"),
  );
  const sessions = [];
  try {
    await setupSchool(workingDirectory);
    const writer = startSession(workingDirectory);
    const reader = startSession(workingDirectory);
    sessions.push(writer, reader);

    writer.child.stdin.write(
      "opendb school;\nbegin;\n" +
        'insert into students (id = 1, name = "Ada");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 300));

    reader.child.stdin.end(
      "opendb school;\nprint students;\nclosedb;\nquit;\n",
    );
    await new Promise((resolve) => setTimeout(resolve, 300));
    assert.equal(reader.child.exitCode, null);

    writer.child.stdin.end("commit;\nclosedb;\nquit;\n");
    await Promise.all([waitForExit(writer), waitForExit(reader)]);
    assert.match(reader.output(), /Ada/);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("read-to-write upgrades cannot let both readers commit stale decisions", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-upgrade-"),
  );
  const sessions = [];
  try {
    await setupSchool(workingDirectory);
    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);

    first.child.stdin.write("opendb school;\nbegin;\nprint students;\n");
    second.child.stdin.write("opendb school;\nbegin;\nprint students;\n");
    await new Promise((resolve) => setTimeout(resolve, 400));

    first.child.stdin.end(
      'insert into students (id = 1, name = "Ada");\n' +
        "commit;\nclosedb;\nquit;\n",
    );
    second.child.stdin.end(
      'insert into students (id = 2, name = "Grace");\n' +
        "commit;\nclosedb;\nquit;\n",
    );
    await Promise.all([waitForExit(first), waitForExit(second)]);

    const output = `${first.output()}\n${second.output()}`;
    assert.ok((output.match(/<ERROR 144>/g) ?? []).length >= 1);

    const read = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint students;\nclosedb;",
    });
    assert.deepEqual(read.errors, []);
    const committedRows = ["Ada", "Grace"].filter((name) =>
      read.stdout.includes(name),
    );
    assert.ok(committedRows.length <= 1);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("two-table deadlocks time out and roll back their victims", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-deadlock-"),
  );
  const sessions = [];
  try {
    await setupSchool(workingDirectory, [
      "create courses (id = i primary key, title = s24);",
    ]);
    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);

    first.child.stdin.write(
      "opendb school;\nbegin;\n" +
        'insert into students (id = 1, name = "Ada");\n',
    );
    second.child.stdin.write(
      "opendb school;\nbegin;\n" +
        'insert into courses (id = 10, title = "Databases");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 400));

    first.child.stdin.end(
      'insert into courses (id = 11, title = "Systems");\n' +
        "commit;\nclosedb;\nquit;\n",
    );
    second.child.stdin.end(
      'insert into students (id = 2, name = "Grace");\n' +
        "commit;\nclosedb;\nquit;\n",
    );
    await Promise.all([waitForExit(first), waitForExit(second)]);

    const output = `${first.output()}\n${second.output()}`;
    const timeoutCount = (output.match(/<ERROR 143>/g) ?? []).length;
    const abortCount = (output.match(/<ERROR 144>/g) ?? []).length;
    assert.ok(timeoutCount >= 1);
    assert.equal(abortCount, timeoutCount);

    const read = await runMinirelScript({
      workingDirectory,
      script:
        "opendb school;\n" +
        "print students;\n" +
        "print courses;\n" +
        "closedb;",
    });
    assert.deepEqual(read.errors, []);
    if (abortCount === 2) {
      assert.doesNotMatch(read.stdout, /Ada|Grace|Databases|Systems/);
    }
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

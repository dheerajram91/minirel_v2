import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
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
    env: { ...process.env, MINIREL_DATA_DIR: "." },
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

async function closeSession(session) {
  session.child.stdin.end("closedb;\nquit;\n");
  await new Promise((resolve, reject) => {
    session.child.once("error", reject);
    session.child.once("exit", resolve);
  });
}

async function closeSessions(sessions) {
  await Promise.all(sessions.map(closeSession));
}

async function createSchoolDatabase(workingDirectory) {
  const createDatabase = await runMinirelScript({
    workingDirectory,
    script: "createdb school;",
  });
  assert.deepEqual(createDatabase.errors, []);
  const createTable = await runMinirelScript({
    workingDirectory,
    script:
      "opendb school;\n" +
      "create students (id = i primary key, name = s24);\n" +
      "closedb;",
  });
  assert.deepEqual(createTable.errors, []);
}

test("same-table writers do not overwrite each other's cached pages", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-lock-"),
  );
  const sessions = [];

  try {
    await createSchoolDatabase(workingDirectory);
    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);

    first.child.stdin.write("opendb school;\nprint students;\n");
    second.child.stdin.write("opendb school;\nprint students;\n");
    await new Promise((resolve) => setTimeout(resolve, 500));

    first.child.stdin.write(
      'insert into students (id = 1, name = "Ada");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    second.child.stdin.write(
      'insert into students (id = 2, name = "Grace");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    await closeSessions([first, second]);

    const read = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint students;\nclosedb;",
    });
    assert.deepEqual(read.errors, []);
    assert.match(read.stdout, /Ada/);
    assert.match(read.stdout, /Grace/);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("primary-key checks remain unique across concurrent writers", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-unique-lock-"),
  );
  const sessions = [];

  try {
    await createSchoolDatabase(workingDirectory);
    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);

    first.child.stdin.write("opendb school;\nprint students;\n");
    second.child.stdin.write("opendb school;\nprint students;\n");
    await new Promise((resolve) => setTimeout(resolve, 500));

    first.child.stdin.write(
      'insert into students (id = 7, name = "Ada");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    second.child.stdin.write(
      'insert into students (id = 7, name = "Grace");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    await closeSessions([first, second]);

    const outputs = `${first.output()}\n${second.output()}`;
    assert.equal((outputs.match(/<ERROR 139>/g) ?? []).length, 1);

    const read = await runMinirelScript({
      workingDirectory,
      script: "opendb school;\nprint students;\nclosedb;",
    });
    assert.deepEqual(read.errors, []);
    assert.equal((read.stdout.match(/\|\s+7\s+\|/g) ?? []).length, 1);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("different-table writers preserve both catalog updates", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-catalog-lock-"),
  );
  const sessions = [];

  try {
    const setup = await runMinirelScript({
      workingDirectory,
      script:
        "createdb school;\n" +
        "opendb school;\n" +
        "create students (id = i primary key, name = s24);\n" +
        "create courses (id = i primary key, title = s24);\n" +
        "closedb;",
    });
    assert.deepEqual(setup.errors, []);

    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);
    first.child.stdin.write(
      'opendb school;\ninsert into students (id = 1, name = "Ada");\n',
    );
    second.child.stdin.write(
      'opendb school;\ninsert into courses (id = 10, title = "Databases");\n',
    );
    await new Promise((resolve) => setTimeout(resolve, 200));
    await closeSessions([first, second]);

    const read = await runMinirelScript({
      workingDirectory,
      script:
        "opendb school;\n" +
        "print students;\n" +
        "print courses;\n" +
        "closedb;",
    });
    assert.deepEqual(read.errors, []);
    assert.match(read.stdout, /Ada/);
    assert.match(read.stdout, /Databases/);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

test("concurrent table creation preserves both catalog definitions", async () => {
  const workingDirectory = await mkdtemp(
    path.join(os.tmpdir(), "minirel-schema-lock-"),
  );
  const sessions = [];

  try {
    const createDatabase = await runMinirelScript({
      workingDirectory,
      script: "createdb school;",
    });
    assert.deepEqual(createDatabase.errors, []);

    const first = startSession(workingDirectory);
    const second = startSession(workingDirectory);
    sessions.push(first, second);
    first.child.stdin.write("opendb school;\n");
    second.child.stdin.write("opendb school;\n");
    await new Promise((resolve) => setTimeout(resolve, 300));

    first.child.stdin.write(
      "create students (id = i primary key, name = s24);\n",
    );
    second.child.stdin.write(
      "create courses (id = i primary key, title = s24);\n",
    );
    await new Promise((resolve) => setTimeout(resolve, 200));
    await closeSessions([first, second]);

    const useTables = await runMinirelScript({
      workingDirectory,
      script:
        "opendb school;\n" +
        'insert into students (id = 1, name = "Ada");\n' +
        'insert into courses (id = 10, title = "Databases");\n' +
        "print students;\n" +
        "print courses;\n" +
        "closedb;",
    });
    assert.deepEqual(useTables.errors, []);
    assert.match(useTables.stdout, /Ada/);
    assert.match(useTables.stdout, /Databases/);
  } finally {
    for (const session of sessions) {
      if (session.child.exitCode === null) {
        session.child.kill();
      }
    }
    await rm(workingDirectory, { recursive: true, force: true });
  }
});

import { spawn } from "node:child_process";
import { access, mkdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

export function defaultBinaryPath() {
  return process.env.MINIREL_BIN
    ? path.resolve(process.env.MINIREL_BIN)
    : path.join(repoRoot, "run", process.platform === "win32" ? "minirel.exe" : "minirel");
}

export function defaultDataDirectory() {
  if (process.env.MINIREL_DATA_DIR) {
    return path.resolve(process.env.MINIREL_DATA_DIR);
  }
  return path.join(repoRoot, "DB");
}

export async function resolveWorkingDirectory(workingDirectory) {
  const requested = workingDirectory ? path.resolve(workingDirectory) : undefined;
  const resolved =
    !requested || requested === repoRoot ? defaultDataDirectory() : requested;
  if (!requested || requested === repoRoot) {
    await mkdir(resolved, { recursive: true });
  }
  return resolved;
}

export async function getMinirelStatus(binaryPath = defaultBinaryPath()) {
  try {
    await access(binaryPath);
    return {
      available: true,
      binaryPath,
      defaultDataDirectory: defaultDataDirectory(),
    };
  } catch {
    return {
      available: false,
      binaryPath,
      defaultDataDirectory: defaultDataDirectory(),
    };
  }
}

export function normalizeScript(script) {
  const trimmed = script.trim();
  if (!trimmed) {
    throw new Error("The MINIREL script cannot be empty.");
  }

  const commands = trimmed.endsWith(";") ? trimmed : `${trimmed};`;
  const hasQuit = /(?:^|[\r\n])\s*quit\s*;/i.test(commands);
  return `${commands}${hasQuit ? "" : "\nquit;"}\n`;
}

export async function runMinirelScript({
  script,
  workingDirectory,
  timeoutMs = 30_000,
  binaryPath = defaultBinaryPath(),
}) {
  const status = await getMinirelStatus(binaryPath);
  if (!status.available) {
    throw new Error(
      `MINIREL executable not found at ${binaryPath}. Run "npm run build:minirel" first.`,
    );
  }

  const cwd = await resolveWorkingDirectory(workingDirectory);
  const input = normalizeScript(script);

  return await new Promise((resolve, reject) => {
    const child = spawn(binaryPath, [], {
      cwd,
      env: { ...process.env, MINIREL_DATA_DIR: "." },
      shell: false,
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"],
    });

    let stdout = "";
    let stderr = "";
    let settled = false;

    const timer = setTimeout(() => {
      child.kill();
      if (!settled) {
        settled = true;
        reject(new Error(`MINIREL timed out after ${timeoutMs} ms.`));
      }
    }, timeoutMs);

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });

    child.on("error", (error) => {
      clearTimeout(timer);
      if (!settled) {
        settled = true;
        reject(error);
      }
    });

    child.on("close", (exitCode, signal) => {
      clearTimeout(timer);
      if (settled) {
        return;
      }
      settled = true;

      if (exitCode !== 0) {
        reject(
          new Error(
            `MINIREL exited with code ${exitCode}${signal ? ` (${signal})` : ""}.\n${stderr}`,
          ),
        );
        return;
      }

      const errors = [...stdout.matchAll(/<ERROR (\d+)>:\s*([^\r\n]*)/g)].map(
        ([, code, message]) => ({ code: Number(code), message }),
      );
      resolve({
        stdout,
        stderr,
        exitCode,
        errors,
        binaryPath,
        workingDirectory: cwd,
      });
    });

    child.stdin.end(input);
  });
}

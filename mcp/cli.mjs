import { spawn } from "node:child_process";
import { mkdir } from "node:fs/promises";

import {
  defaultBinaryPath,
  defaultDataDirectory,
  getMinirelStatus,
} from "./minirel-runner.mjs";

const binaryPath = defaultBinaryPath();
const status = await getMinirelStatus(binaryPath);
if (!status.available) {
  console.error(
    `MINIREL executable not found at ${binaryPath}. Run "npm run build:minirel" first.`,
  );
  process.exitCode = 1;
} else {
  const dataDirectory = defaultDataDirectory();
  await mkdir(dataDirectory, { recursive: true });
  console.log(`MINIREL database directory: ${dataDirectory}`);

  const child = spawn(binaryPath, [], {
    cwd: dataDirectory,
    env: { ...process.env, MINIREL_DATA_DIR: "." },
    shell: false,
    windowsHide: true,
    stdio: "inherit",
  });
  child.once("error", (error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
  child.once("exit", (code, signal) => {
    process.exitCode = code ?? (signal ? 1 : 0);
  });
}

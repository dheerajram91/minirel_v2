import { McpServer } from "@modelcontextprotocol/server";
import { StdioServerTransport } from "@modelcontextprotocol/server/stdio";
import { z } from "zod";

import {
  defaultBinaryPath,
  getMinirelStatus,
  runMinirelScript,
} from "./minirel-runner.mjs";

const server = new McpServer({
  name: "minirel",
  version: "0.1.0",
});

server.registerTool(
  "minirel_status",
  {
    title: "Check MINIREL status",
    description: "Check whether the local MINIREL executable is available.",
    inputSchema: z.object({}),
  },
  async () => {
    const status = await getMinirelStatus();
    return {
      content: [
        {
          type: "text",
          text: status.available
            ? `MINIREL is available at ${status.binaryPath}.`
            : `MINIREL is not built. Expected executable: ${status.binaryPath}`,
        },
      ],
      structuredContent: status,
    };
  },
);

server.registerTool(
  "run_minirel_script",
  {
    title: "Run a MINIREL script",
    description:
      "Run semicolon-terminated MINIREL commands. Use `create students (id = i unique, name = s50);` for an optional field-level UNIQUE constraint. A quit command is appended automatically.",
    inputSchema: z.object({
      script: z.string().min(1).describe("MINIREL commands to execute."),
      workingDirectory: z
        .string()
        .optional()
        .describe("Directory used for relative database and data-file paths."),
      timeoutMs: z
        .number()
        .int()
        .min(100)
        .max(120_000)
        .default(30_000)
        .describe("Maximum execution time in milliseconds."),
    }),
  },
  async ({ script, workingDirectory, timeoutMs }) => {
    try {
      const result = await runMinirelScript({
        script,
        workingDirectory,
        timeoutMs,
      });
      return {
        isError: result.errors.length > 0,
        content: [
          {
            type: "text",
            text: result.stdout || "(MINIREL produced no standard output.)",
          },
          ...(result.stderr
            ? [{ type: "text", text: `stderr:\n${result.stderr}` }]
            : []),
        ],
        structuredContent: result,
      };
    } catch (error) {
      return {
        isError: true,
        content: [
          {
            type: "text",
            text: error instanceof Error ? error.message : String(error),
          },
        ],
      };
    }
  },
);

const transport = new StdioServerTransport();
await server.connect(transport);

process.on("SIGINT", async () => {
  await server.close();
  process.exit(0);
});

process.stderr.write(`MINIREL MCP server using ${defaultBinaryPath()}\n`);

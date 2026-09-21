import { McpServer } from "@modelcontextprotocol/server";
import { StdioServerTransport } from "@modelcontextprotocol/server/stdio";
import { z } from "zod";

import {
  discoverMinirelDatabases,
  inspectMinirelDatabase,
} from "./databases.mjs";
import { MINIREL_HELP } from "./help.mjs";
import {
  defaultBinaryPath,
  getMinirelStatus,
  runMinirelScript,
} from "./minirel-runner.mjs";
import {
  buildTransactionScript,
  normalizeCommands,
} from "./transactions.mjs";

const server = new McpServer({
  name: "minirel",
  version: "0.2.0",
});

function toolResult(result) {
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
}

function toolError(error) {
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

async function executeScript(script, workingDirectory, timeoutMs) {
  try {
    return toolResult(
      await runMinirelScript({ script, workingDirectory, timeoutMs }),
    );
  } catch (error) {
    return toolError(error);
  }
}

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
  "run_minirel_commands",
  {
    title: "Run MINIREL commands",
    description:
      "Run commands in order. Use this for database lifecycle, reads, and advanced commands. Prefer execute_transaction for atomic changes inside an existing database.",
    inputSchema: z.object({
      commands: z
        .array(z.string().min(1))
        .min(1)
        .describe("Commands to run in order, with or without trailing semicolons."),
      workingDirectory: z
        .string()
        .optional()
        .describe(
          "Optional database directory override. Omit it or pass the repository root to use the ignored DB directory.",
        ),
      timeoutMs: z.number().int().min(100).max(120_000).default(30_000),
    }),
  },
  async ({ commands, workingDirectory, timeoutMs }) => {
    try {
      return await executeScript(
        normalizeCommands(commands).join("\n"),
        workingDirectory,
        timeoutMs,
      );
    } catch (error) {
      return toolError(error);
    }
  },
);

server.registerTool(
  "discover_databases",
  {
    title: "Discover MINIREL databases",
    description:
      "Find MINIREL databases under a directory and report each database's table names and record counts.",
    inputSchema: z.object({
      workingDirectory: z
        .string()
        .optional()
        .describe(
          "Optional database directory override. The repository root resolves to its ignored DB directory.",
        ),
    }),
  },
  async ({ workingDirectory }) => {
    try {
      const result = await discoverMinirelDatabases(workingDirectory);
      const text =
        result.databaseCount === 0
          ? `No MINIREL databases found in ${result.workingDirectory}.`
          : result.databases
              .map((database) => {
                const tables = database.tables
                  .map((table) => `${table.name} (${table.recordCount} records)`)
                  .join(", ");
                return `${database.name}: ${tables || "(no user tables)"}${
                  database.inspectionError
                    ? ` [inspection failed: ${database.inspectionError}]`
                    : ""
                }`;
              })
              .join("\n");
      return {
        content: [{ type: "text", text }],
        structuredContent: result,
      };
    } catch (error) {
      return toolError(error);
    }
  },
);

server.registerTool(
  "discover_tables",
  {
    title: "Discover tables and schemas",
    description:
      "Inspect one MINIREL database and return table names, column schemas, constraints, record counts, page counts, and storage sizes.",
    inputSchema: z.object({
      database: z.string().min(1),
      workingDirectory: z
        .string()
        .optional()
        .describe(
          "Optional database directory override. The repository root resolves to its ignored DB directory.",
        ),
      includeSystemTables: z.boolean().default(false),
    }),
  },
  async ({ database, workingDirectory, includeSystemTables }) => {
    try {
      const result = await inspectMinirelDatabase({
        database,
        workingDirectory,
        includeSystemTables,
      });
      const text =
        result.tableCount === 0
          ? `Database ${database} has no matching tables.`
          : result.tables
              .map(
                (table) =>
                  `${table.name}: ${table.recordCount} records; ` +
                  table.columns
                    .map((column) => {
                      const constraints = [
                        column.primaryKey && "primary key",
                        column.unique && !column.primaryKey && "unique",
                        column.notNull && !column.primaryKey && "not null",
                      ].filter(Boolean);
                      return `${column.name} ${column.type}${
                        column.type === "string" ? `(${column.length})` : ""
                      }${constraints.length ? ` ${constraints.join(" ")}` : ""}`;
                    })
                    .join(", "),
              )
              .join("\n");
      return {
        content: [{ type: "text", text }],
        structuredContent: result,
      };
    } catch (error) {
      return toolError(error);
    }
  },
);

server.registerTool(
  "execute_transaction",
  {
    title: "Execute a serializable transaction",
    description:
      "Execute commands atomically inside an existing database. MINIREL adds opendb, begin, commit, and closedb. Do not include transaction or database lifecycle commands.",
    inputSchema: z.object({
      database: z.string().min(1),
      commands: z.array(z.string().min(1)).min(1),
      workingDirectory: z
        .string()
        .optional()
        .describe(
          "Optional database directory override. The repository root resolves to its ignored DB directory.",
        ),
      timeoutMs: z.number().int().min(100).max(120_000).default(30_000),
    }),
  },
  async ({ database, commands, workingDirectory, timeoutMs }) => {
    try {
      return await executeScript(
        buildTransactionScript(database, commands),
        workingDirectory,
        timeoutMs,
      );
    } catch (error) {
      return toolError(error);
    }
  },
);

server.registerPrompt(
  "minirel_help",
  {
    title: "MINIREL command and transaction help",
    description:
      "List supported MINIREL commands, schema syntax, realistic school/library/course examples, and transaction composition.",
    argsSchema: z.object({}),
  },
  () => ({
    messages: [
      {
        role: "user",
        content: { type: "text", text: MINIREL_HELP },
      },
    ],
  }),
);

const transport = new StdioServerTransport();
await server.connect(transport);

process.on("SIGINT", async () => {
  await server.close();
  process.exit(0);
});

process.stderr.write(`MINIREL MCP server using ${defaultBinaryPath()}\n`);

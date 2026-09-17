import { validateIdentifier } from "./commands.mjs";

const TRANSACTION_CONTROL =
  /^(?:begin|commit|rollback|opendb|closedb|quit|createdb|destroydb)\b/i;

export function normalizeCommand(command, index = 0) {
  if (typeof command !== "string") {
    throw new Error(`Command ${index + 1} must be a string.`);
  }

  const trimmed = command.trim();
  if (!trimmed) {
    throw new Error(`Command ${index + 1} cannot be empty.`);
  }
  if (/[\r\n]/.test(trimmed)) {
    throw new Error(`Command ${index + 1} must be supplied on one line.`);
  }

  const withoutTerminator = trimmed.endsWith(";")
    ? trimmed.slice(0, -1).trim()
    : trimmed;
  if (!withoutTerminator || withoutTerminator.includes(";")) {
    throw new Error(`Command ${index + 1} must contain exactly one command.`);
  }
  return `${withoutTerminator};`;
}

export function normalizeCommands(commands) {
  if (!Array.isArray(commands) || commands.length === 0) {
    throw new Error("At least one MINIREL command is required.");
  }
  return commands.map(normalizeCommand);
}

export function buildTransactionScript(database, commands) {
  validateIdentifier(database, "Database name");
  const normalized = normalizeCommands(commands);

  for (const [index, command] of normalized.entries()) {
    if (TRANSACTION_CONTROL.test(command)) {
      throw new Error(
        `Command ${index + 1} controls a database or transaction. ` +
          "Pass only commands that belong inside the transaction.",
      );
    }
  }

  return [
    `opendb ${database};`,
    "begin;",
    ...normalized,
    "commit;",
    "closedb;",
  ].join("\n");
}

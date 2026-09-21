import { readdir, stat } from "node:fs/promises";
import path from "node:path";

import { validateIdentifier } from "./commands.mjs";
import {
  resolveWorkingDirectory,
  runMinirelScript,
} from "./minirel-runner.mjs";

const SYSTEM_TABLES = new Set(["relcat", "attrcat"]);
const ATTRIBUTE_TYPE_MASK = 0xff;
const UNIQUE_ATTRIBUTE_FLAG = 0x100;
const NOT_NULL_ATTRIBUTE_FLAG = 0x200;
const PRIMARY_KEY_ATTRIBUTE_FLAG = 0x400;
const NULL_BITMAP_STORAGE_FLAG = 0x800;

async function isMinirelDatabase(root, entry) {
  if (!entry.isDirectory()) {
    return false;
  }

  try {
    const [relationCatalog, attributeCatalog] = await Promise.all([
      stat(path.join(root, entry.name, "relcat")),
      stat(path.join(root, entry.name, "attrcat")),
    ]);
    return relationCatalog.isFile() && attributeCatalog.isFile();
  } catch (error) {
    if (error?.code === "ENOENT" || error?.code === "ENOTDIR") {
      return false;
    }
    throw error;
  }
}

export async function listMinirelDatabases(workingDirectory) {
  const root = await resolveWorkingDirectory(workingDirectory);
  const entries = await readdir(root, { withFileTypes: true });
  const matches = await Promise.all(
    entries.map(async (entry) => ({
      name: entry.name,
      matches: await isMinirelDatabase(root, entry),
    })),
  );

  return {
    workingDirectory: root,
    databases: matches
      .filter((entry) => entry.matches)
      .map((entry) => entry.name)
      .sort((left, right) => left.localeCompare(right)),
  };
}

function parseTableRows(stdout) {
  const relations = [];
  const attributes = [];
  let section;

  for (const line of stdout.split(/\r?\n/)) {
    if (!line.trimStart().startsWith("|")) {
      continue;
    }

    const cells = line
      .split("|")
      .slice(1, -1)
      .map((cell) => cell.trim());

    if (
      cells.join("|") ===
      "relName|recLength|recsPerPg|numAttrs|numRecs|numPgs"
    ) {
      section = "relations";
      continue;
    }
    if (cells.join("|") === "offset|length|type|attrName|relName") {
      section = "attributes";
      continue;
    }

    if (section === "relations" && cells.length === 6) {
      relations.push({
        name: cells[0],
        recordLength: Number(cells[1]),
        recordsPerPage: Number(cells[2]),
        columnCount: Number(cells[3]),
        recordCount: Number(cells[4]),
        pageCount: Number(cells[5]),
      });
    } else if (section === "attributes" && cells.length === 5) {
      const encodedType = Number(cells[2]);
      const baseType = encodedType & ATTRIBUTE_TYPE_MASK;
      const type =
        baseType === "i".charCodeAt(0)
          ? "integer"
          : baseType === "f".charCodeAt(0)
            ? "float"
            : baseType === "s".charCodeAt(0)
              ? "string"
              : "unknown";

      attributes.push({
        offset: Number(cells[0]),
        length: Number(cells[1]),
        type,
        name: cells[3],
        relation: cells[4],
        unique: Boolean(encodedType & UNIQUE_ATTRIBUTE_FLAG),
        notNull: Boolean(encodedType & NOT_NULL_ATTRIBUTE_FLAG),
        primaryKey: Boolean(encodedType & PRIMARY_KEY_ATTRIBUTE_FLAG),
        nullBitmapStorage: Boolean(encodedType & NULL_BITMAP_STORAGE_FLAG),
      });
    }
  }

  return { relations, attributes };
}

export async function inspectMinirelDatabase({
  database,
  workingDirectory,
  includeSystemTables = false,
}) {
  validateIdentifier(database, "Database name");
  const root = await resolveWorkingDirectory(workingDirectory);
  const result = await runMinirelScript({
    workingDirectory: root,
    script: [
      `opendb ${database};`,
      "print relcat;",
      "print attrcat;",
      "closedb;",
    ].join("\n"),
  });

  if (result.errors.length > 0) {
    throw new Error(
      `Unable to inspect database "${database}": ${result.errors
        .map((error) => `<ERROR ${error.code}>: ${error.message}`)
        .join(" ")}`,
    );
  }

  const { relations, attributes } = parseTableRows(result.stdout);
  const tables = relations
    .filter((relation) => includeSystemTables || !SYSTEM_TABLES.has(relation.name))
    .map((relation) => {
      const relationAttributes = attributes
        .filter((attribute) => attribute.relation === relation.name)
        .sort((left, right) => left.offset - right.offset);
      const hasNullBitmap = relationAttributes.some(
        (attribute) => attribute.nullBitmapStorage,
      );
      const nullBitmapBytes = hasNullBitmap
        ? Math.ceil(relation.columnCount / 8)
        : 0;

      return {
        ...relation,
        systemTable: SYSTEM_TABLES.has(relation.name),
        recordFormat: hasNullBitmap ? "null-bitmap-v1" : "legacy-fixed-v1",
        dataLength: relation.recordLength - nullBitmapBytes,
        nullBitmapBytes,
        columns: relationAttributes.map(
          ({ relation: _relation, nullBitmapStorage: _storage, ...attribute }) => ({
            ...attribute,
            nullable: !attribute.notNull,
          }),
        ),
      };
    })
    .sort((left, right) => left.name.localeCompare(right.name));

  return {
    database,
    databasePath: path.join(root, database),
    tableCount: tables.length,
    tables,
  };
}

export async function discoverMinirelDatabases(
  workingDirectory,
) {
  const listed = await listMinirelDatabases(workingDirectory);
  const databases = await Promise.all(
    listed.databases.map(async (database) => {
      try {
        const inspected = await inspectMinirelDatabase({
          database,
          workingDirectory: listed.workingDirectory,
        });
        return {
          name: database,
          path: inspected.databasePath,
          tableCount: inspected.tableCount,
          tables: inspected.tables.map(({ name, recordCount }) => ({
            name,
            recordCount,
          })),
        };
      } catch (error) {
        return {
          name: database,
          path: path.join(listed.workingDirectory, database),
          tableCount: 0,
          tables: [],
          inspectionError: error instanceof Error ? error.message : String(error),
        };
      }
    }),
  );

  return {
    workingDirectory: listed.workingDirectory,
    databaseCount: databases.length,
    databases,
  };
}

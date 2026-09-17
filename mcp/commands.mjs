const IDENTIFIER_PATTERN = /^[A-Za-z][A-Za-z0-9-]{0,18}$/;

export function validateIdentifier(value, label) {
  if (!IDENTIFIER_PATTERN.test(value)) {
    throw new Error(
      `${label} must start with a letter, contain only letters, digits, or hyphens, and be at most 19 characters.`,
    );
  }
  return value;
}

export function formatValue(value) {
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      throw new Error("Numeric values must be finite.");
    }
    return String(value);
  }

  if (typeof value !== "string") {
    throw new Error("MINIREL row values must be strings or numbers.");
  }
  if (/[\r\n";]/.test(value)) {
    throw new Error(
      'String values cannot contain quotes, semicolons, or line breaks.',
    );
  }
  return `"${value}"`;
}

export function buildCreateDatabaseCommand(database) {
  return `createdb ${validateIdentifier(database, "Database name")};`;
}

export function buildDestroyDatabaseCommand(database) {
  return `destroydb ${validateIdentifier(database, "Database name")};`;
}

export function buildCreateRelationCommand(relation, columns) {
  validateIdentifier(relation, "Relation name");
  if (!Array.isArray(columns) || columns.length === 0) {
    throw new Error("At least one column is required.");
  }

  const primaryKeys = columns.filter((column) => column.primaryKey);
  if (primaryKeys.length > 1) {
    throw new Error("A relation can declare only one primary key.");
  }

  const seenNames = new Set();
  const definitions = columns.map((column) => {
    validateIdentifier(column.name, "Column name");
    if (seenNames.has(column.name)) {
      throw new Error(`Column "${column.name}" is declared more than once.`);
    }
    seenNames.add(column.name);

    let type;
    switch (column.type) {
      case "integer":
        type = "i";
        break;
      case "float":
        type = "f";
        break;
      case "string":
        if (
          !Number.isInteger(column.length) ||
          column.length < 1 ||
          column.length > 50
        ) {
          throw new Error(
            `String column "${column.name}" requires a length from 1 to 50.`,
          );
        }
        type = `s${column.length}`;
        break;
      default:
        throw new Error(`Unsupported type for column "${column.name}".`);
    }

    const constraints = [];
    if (column.primaryKey) {
      constraints.push("primary key");
    } else {
      if (column.unique) {
        constraints.push("unique");
      }
      if (column.notNull) {
        constraints.push("not null");
      }
    }

    return `${column.name} = ${type}${
      constraints.length ? ` ${constraints.join(" ")}` : ""
    }`;
  });

  return `create ${relation} (${definitions.join(", ")});`;
}

export function buildInsertCommands(relation, rows) {
  validateIdentifier(relation, "Relation name");
  if (!Array.isArray(rows) || rows.length === 0) {
    throw new Error("At least one row is required.");
  }

  return rows.map((row, rowIndex) => {
    if (typeof row !== "object" || row === null || Array.isArray(row)) {
      throw new Error(`Row ${rowIndex + 1} must be an object.`);
    }

    const entries = Object.entries(row);
    if (entries.length === 0) {
      throw new Error(`Row ${rowIndex + 1} cannot be empty.`);
    }

    const assignments = entries.map(([name, value]) => {
      validateIdentifier(name, "Column name");
      return `${name} = ${formatValue(value)}`;
    });
    return `insert into ${relation} (${assignments.join(", ")});`;
  });
}

export function buildOpenDatabaseScript(database, commands) {
  validateIdentifier(database, "Database name");
  return [`opendb ${database};`, ...commands, "closedb;"].join("\n");
}

export function buildReadRelationCommand(relation) {
  return `print ${validateIdentifier(relation, "Relation name")};`;
}

export function buildDropRelationCommand(relation) {
  return `destroy ${validateIdentifier(relation, "Relation name")};`;
}

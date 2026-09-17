export const MINIREL_HELP = `# MINIREL command help

Every command must end with a semicolon and be sent on its own line.

## Database lifecycle

\`\`\`text
createdb school;
opendb school;
closedb;
destroydb school;
quit;
\`\`\`

## Tables and rows

\`\`\`text
create students (id = i primary key, name = s24 not null, email = s50 unique);
create courses (id = i primary key, title = s40 not null);
create library (bookid = i primary key, title = s50, borrowerid = i);

insert into students (id = 1, name = "Ada", email = "ada@school");
insert into courses (id = 10, title = "Database Systems");
insert into library (bookid = 100, title = "Database Internals", borrowerid = 1);

print students;
update students set name = "Ada Lovelace" where (id = 1);
update library set borrowerid = 2 where (bookid = 100 and borrowerid = 1);
delete from library where (borrowerid = 1);
destroy library;
\`\`\`

Types are \`i\` (integer), \`f\` (float), and \`sN\` (a string of 1-50 bytes).
Columns may use \`unique\`, \`not null\`, or \`primary key\`. A primary key
implies unique and not-null behavior.

## Relational operations

\`\`\`text
select into namedstudents from students where (name = "Ada");
project into coursenames from courses (id, title);
join into registrations (students.id, enrollments.studentid);
load students from students.data;
\`\`\`

Comparison operators are \`=\`, \`>=\`, \`>\`, \`<=\`, \`<>\`, and \`<\`.
\`update\` accepts multiple comma-separated assignments and its conditions may
be joined with \`and\`. \`select\` and \`delete\` currently evaluate one
condition.
\`load\` consumes MINIREL's fixed-width binary record format, not CSV.

\`buildindex for students on id;\` and \`dropindex for students on id;\` are
accepted by the parser but indexing is only a stub and does not create an index.
\`sort\`, \`intersect\`, \`union\`, and the native \`help\` command are not
implemented.

## Serializable transaction

\`\`\`text
opendb school;
begin;
insert into students (id = 2, name = "Grace", email = "grace@school");
insert into courses (id = 20, title = "Transaction Processing");
commit;
closedb;
\`\`\`

Use \`rollback;\` instead of \`commit;\` to discard the transaction. The MCP
\`execute_transaction\` tool adds \`opendb\`, \`begin\`, \`commit\`, and
\`closedb\` automatically; pass only the commands that belong inside the
transaction. If any command fails, MINIREL marks the transaction failed and
the final commit rolls it back. Explicit transactions are protected by the
database-local \`.minirel.wal\`; recovery runs automatically during the next
\`opendb\` after a crash. Commands outside an explicit transaction are not yet
WAL-protected, so use \`execute_transaction\` for changes that need crash
atomicity and durability.
`;

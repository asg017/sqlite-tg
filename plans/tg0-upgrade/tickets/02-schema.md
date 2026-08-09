# 02 — Shadow tables + lifecycle methods

Create/connect/destroy for the new storage layout (decisions 4, 6; "Shadow
tables" section of plan.md). Replaces the everything-in-rtree layout.

## Tasks

1. `xCreate`: create shadow tables exactly as specified in plan.md —
   `t_info`, `t_content` (with `rowid INTEGER PRIMARY KEY` — do NOT copy
   vec0's SHADOW_TABLE_ROWID_QUIRK), one `t_rtree%02d` per geometry column,
   `t_rowids` only for text pk. Write `CREATE_VERSION` into `t_info`.
2. `xConnect`: re-parse args (ticket 01), no DDL. Keep the
   `db_supports_rtree` probe for both paths with the existing error message.
3. `xDestroy`: drop all shadow tables; surface drop failures via
   `sqlite3_errmsg` instead of silently ignoring (fixes current
   tg0Destroy behavior at sqlite-tg.c:2792-2795).
4. `xShadowName`: `info`, `content`, `rowids`, `rtree%02d`, plus rtree's own
   `rtree%02d_node/_parent/_rowid`. Note: xShadowName gets suffixes only, so
   match prefixes `rtree` + digits robustly.
5. vtab struct rework: schemaName/tableName + parsed column model from
   ticket 01; precompute per-geometry-column rtree table names.
6. Delete the old single-rtree DDL and `TG0_SQL_*` templates.

## Acceptance

- `sqlite_master` snapshot test for: no pk, int pk, text pk, multi-geometry
  tables (shows content/info/rtreeNN/rowids as applicable).
- CREATE → close → reopen → SELECT (empty) works (xConnect path).
- `DROP TABLE` removes every shadow table (snapshot of sqlite_master after).
- Old-layout DB fixture (created with previous tg0 build, committed as a test
  asset or constructed by SQL in the test): opening errors with the migration
  hint from ticket 01.

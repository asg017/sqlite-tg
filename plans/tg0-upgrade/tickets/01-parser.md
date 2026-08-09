# 01 — Constructor tokenizer + column parser + declare_vtab

## Status: complete (0874b04)

New `CREATE VIRTUAL TABLE ... USING tg0(...)` parser per the grammar in
`plan.md` (decisions 1, 3, 5, 6, 7, 16). Model on sqlite-vec's
`vec0_token_next` / `vec0_parse_*` (sqlite-vec.c:2072-2540) but fix its known
warts: use `||` (not `&&`) when checking token-result + token-type, and keep
parsers as small pure functions over `(const char *, int)`.

## Tasks

1. Tokenizer: `[A-Za-z][A-Za-z0-9_]*` identifiers, digits, `=`, `,`;
   case-insensitive keyword matching (`sqlite3_strnicmp`).
2. Column-definition parsers, tried most-specific-first per arg:
   1. primary key: `name integer primary key` | `name text primary key`
   2. geometry: `name <geom-type> [indexing=none|natural|ystripes]*`
      (geom-type: geometry/point/linestring/polygon/multipoint/
      multilinestring/multipolygon/geometrycollection; map indexing to
      `TG_NONE`/`TG_NATURAL`/`TG_YSTRIPES`, default `TG_DEFAULT`)
   3. data: `name text|int|integer|float|double|blob` (store the SQLITE_*
      type code)
3. Struct definitions: `tg0_geometry_column` (name, tg type or 0=generic,
   tg_index), `tg0_data_column` (name, type code), pk kind/name; parallel
   `user_column_kinds[]` / `user_column_idxs[]` arrays indexed by declaration
   position (vec0 pattern, sqlite-vec.c:3538-3540).
4. Validation: ≥1 geometry column; caps `TG0_MAX_GEOMETRY_COLUMNS 8`,
   `TG0_MAX_DATA_COLUMNS 16`; ≤1 primary key; reject user columns named
   `distance` or `rowid`; duplicate column names rejected.
5. Error messages: `tg0 constructor error: ...`, always naming the offending
   arg. Special case: a bare identifier with no type (the old syntax) gets
   "tg0's syntax changed in vX.Y.Z: columns now require types, and the _shape
   column was removed. Drop and recreate this table. See <docs url>."
6. `declare_vtab` string: pk (or nothing) + user columns in declaration order
   + `distance hidden`; append ` WITHOUT ROWID` for text pk (decisions 6, 9).
7. Wire into `tg0_init` replacing the verbatim-paste loop
   (sqlite-tg.c:2695-2712). Shadow DDL is ticket 02 — for this ticket the
   old rtree DDL may be temporarily left broken/behind a stub as long as the
   branch's tests pass at the CREATE level.

## Acceptance

- CREATE-level tests in `tests/test-loadable.py` covering: every column kind
  parses; free-form declaration order; all caps and validation errors with
  exact messages (snapshot); old-syntax `tg0(tzid)` produces the migration
  hint; `indexing=` accepted only on geometry columns.
- `PRAGMA table_info` / `pragma table_xinfo` snapshot shows declared columns +
  hidden `distance` (and WITHOUT ROWID handling for text pk).

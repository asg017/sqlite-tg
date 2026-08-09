# tg0 v2 — typed geometry columns, real schema, full predicate support

Rebuild the `tg0` virtual table from the current `_shape`-plus-verbatim-aux
design into a vec0-inspired typed-column schema. This is a **clean break**:
tg0 is documented as experimental alpha ("expect breaking changes"), and no
backwards compatibility with the old shadow layout or syntax is kept.

Tickets live in `plans/tg0-upgrade/tickets/*.md`, worked in order (dependency
order at the bottom). Conventions follow the update workstream
(`todos/update/context.md`): one branch per ticket, serialize tickets, per
user-visible change update `reference.yaml` → `make docs` / `make docs-check` +
`site/build-ref.mjs`, flip the matching NOTES checkbox, add snapshot tests, run
`make test-loadable`, `make test-api`, `make test-memory`, `tests/test-valgrind.sh`.

## Current state (what's being replaced)

Everything lives in `sqlite-tg.c:2606-3320` (`#pragma region tg0 virtual table`).

- Syntax: `CREATE VIRTUAL TABLE t USING tg0(aux1, aux2, ...)` — args pasted
  verbatim into `declare_vtab`, implicit `_shape` column at index 0.
- Storage: one shadow table, itself an rtree —
  `t_rtree using rtree(id, minX, maxX, minY, maxY, +_shape BLOB, +c1, ...)`.
  Geometry WKB and all aux values live in unindexed rtree `+` columns, so rowid
  lookups are full scans and the rtree is bloated by payload.
- Predicates: `xFindFunction` intercepts 8 two-arg predicates, but only
  intersects/touches/equals execute; disjoint/contains/within/covers/coveredby
  **error at query time**. Two predicates in one WHERE clause is a hard error.
  `iColumn` is ignored, so `tg_intersects(aux_col, x)` is wrongly intercepted.
- Writes: INSERT and DELETE only; UPDATE errors. `*pRowid` never set on INSERT
  so `last_insert_rowid()` is broken. Geometries stored as plain WKB, always
  re-parsed with `TG_NONE` — tg's `TG_NATURAL`/`TG_YSTRIPES` polygon indexes are
  unreachable, even though they are the whole point of tg for big polygons.
- `xRename` unimplemented; several error paths report nothing useful.

## Decisions

Settled 2026-08-09 (grilling session). Numbered for reference from tickets.

1. **Typed geometry columns, generic + narrowing.** A geometry column is
   declared `name geometry` (accepts all 7 tg types) or narrowed to one of
   `point | linestring | polygon | multipoint | multilinestring | multipolygon |
   geometrycollection`, enforced on write. There is no implicit `_shape`.
2. **Clean break.** New parser only. An old-layout table fails `xConnect` with
   a clear "tg0 syntax changed … recreate the table" error (the old bare-arg
   syntax is detected as a bare identifier with no type). In-repo examples and
   tests are migrated in this workstream.
3. **Multiple geometry columns** per table, each with its own bbox rtree.
   Cap: `TG0_MAX_GEOMETRY_COLUMNS 8`. At least one geometry column is required.
4. **Content table + slim rtrees.** Row data lives in an ordinary shadow table;
   rtrees hold only `(id, minX, maxX, minY, maxY)`. Rowid/PK lookups become
   B-tree point queries; float32 bbox rounding is confined to the index.
5. **One kind of non-geometry column: plain data columns** —
   `name text|integer|float|blob`, stored in the content table, nullable,
   type-enforced on write (mismatch = `SQLITE_CONSTRAINT`, like vec0 aux).
   No vec0-style metadata/aux/partition kinds: vec0 needs them because its KNN
   materializes top-k inside the scan (pre-filter is a *correctness* issue);
   tg0 streams unbounded predicate results, so SQLite's own post-filtering is
   always correct and pushdown is purely an optimization (decision 10).
   Partition keys (per-value rtree sharding for stacked-layer workloads) are
   explicitly deferred — addable later as new syntax with no migration.
   Cap: `TG0_MAX_DATA_COLUMNS 16`.
6. **Primary keys: integer and text (full vec0 parity).**
   - `id integer primary key` → named rowid alias (content table IPK).
   - `id text primary key` → `WITHOUT ROWID` declare_vtab; a `t_rowids`
     mapping table assigns int64 rowids (needed by rtree) to text ids.
   - No PK declared → plain rowid behavior.
7. **Geometry storage: plain WKB** in the content table (portable, feeds every
   `tg_*` function directly). Per-geometry-column option
   `indexing=none|natural|ystripes` controls the `tg_index` used when parsing
   *stored candidates* at query time (`tg_parse_wkb_ix`); default
   `TG_DEFAULT` (natural when worthwhile). Write-side parses that only need a
   bbox use `TG_NONE`.
8. **All 8 predicates work; none ever error.** rtree strategy per predicate:

   | predicate | rtree prefilter | note |
   |---|---|---|
   | tg_intersects | bbox overlap | |
   | tg_touches | bbox overlap | |
   | tg_equals | bbox overlap | (bbox equality not exploited; fine) |
   | tg_contains | candidate bbox ⊇ query bbox | swap-pair of within |
   | tg_covers | candidate bbox ⊇ query bbox | |
   | tg_within | candidate bbox ⊆ query bbox | see float32 caveat below |
   | tg_coveredby | candidate bbox ⊆ query bbox | |
   | tg_disjoint | none — full scan + exact test | matches are outside the bbox |

   Multiple predicates in one WHERE clause are allowed: xBestIndex consumes
   exactly one (preferring any non-disjoint predicate) for the rtree plan and
   leaves the rest unconsumed — SQLite evaluates them as ordinary scalar
   functions against the WKB blobs xColumn returns.
9. **KNN: designed for, shipped later.** This workstream reserves the surface —
   a hidden `distance` column in declare_vtab and a spare plan code in the
   idxStr encoding — but implements no KNN. Follow-up adds best-first rtree
   traversal (`sqlite3_rtree_query_callback` or direct node walking) behind
   `WHERE geomcol MATCH tg_knn(:x,:y) ORDER BY distance LIMIT n`. Because tg0
   streams nearest-first incrementally (no internal top-k truncation), SQLite
   applying WHERE-then-LIMIT on the stream stays correct even with extra
   filters — no vec0-style pre-filter machinery will be needed.
10. **Constraint pushdown as optimization** (not syntax): EQ/NE/LT/LE/GT/GE
    constraints on plain data columns *may* be folded into the content-table
    WHERE clause so cheap B-tree compares run before WKB parse + exact
    predicate. Correctness never depends on it. (Ticket 05; can slip.)
11. **Geometry columns are NOT NULL** (vec0-style). Inserting NULL (or omitting
    a geometry column) is `SQLITE_CONSTRAINT`. Rtrees and content stay strictly
    1:1 — every content row has exactly one entry in every rtree.
12. **Strict typing, no coercion.** A `multipolygon` column rejects a Polygon
    (`SQLITE_CONSTRAINT`, message names column, expected, and actual type via
    `tg_geom_type_string`). `geometry` is the escape hatch for mixed data.
    Single→multi promotion, if ever, arrives as an explicit SQL function, not
    implicit vtab behavior.
13. **Reads return WKB blobs**, exactly as today. Input coercion unchanged:
    `geomValue()` sniffing (WKB blob / GeoJSON text / WKT text / tg_geom
    pointer). A pointer-passing fast path inside the vtab may come later
    without changing the observable type.
14. **UPDATE fully supported** for geometry and data columns (content row
    update + rtree entry update). Changing the rowid/PK value itself in an
    UPDATE is unsupported initially → clear error.
15. **Ride-along fixes** (vs. current implementation): set `*pRowid` on INSERT;
    check `iColumn` in xBestIndex (predicates on non-geometry columns are not
    intercepted); rowid/PK EQ becomes a point plan; implement `xRename`;
    real error messages (use/replace `tg_vtab_set_error`, include
    `sqlite3_errmsg` on prepare/step failures); fix the DELETE-branch
    return-OK-on-error bug; audit the three leak-on-OOM TODO sites.
16. **Parser: vec0-style hand-rolled tokenizer**, most-specific-first parse
    order, but do NOT inherit vec0's warts: use `||` (not `&&`) in
    token-result checks, and use `rowid INTEGER PRIMARY KEY` in shadow tables
    (avoid vec0's SHADOW_TABLE_ROWID_QUIRK — see sqlite-vec.c:3443-3450).
    Identifiers: `[A-Za-z][A-Za-z0-9_]*`, keywords case-insensitive, no quoted
    identifiers (same restriction as vec0; lift later if needed).

## Constructor grammar

```
tg0( column-def ["," column-def]* )

column-def :=
    name INTEGER PRIMARY KEY                     -- rowid alias (≤1 pk total)
  | name TEXT PRIMARY KEY                        -- WITHOUT ROWID + rowids map
  | name geom-type [geom-option]*                -- geometry column (1..8)
  | name TEXT | INT | INTEGER | FLOAT | DOUBLE | BLOB   -- data column (0..16)

geom-type   := GEOMETRY | POINT | LINESTRING | POLYGON
             | MULTIPOINT | MULTILINESTRING | MULTIPOLYGON | GEOMETRYCOLLECTION
geom-option := INDEXING "=" (NONE | NATURAL | YSTRIPES)
```

Parse order per arg (most specific first, first match wins): primary key →
geometry column → data column. Anything else → `tg0 constructor error: could
not parse '%s'`; a bare identifier with no type additionally hints that the old
tg0 syntax was removed and the table must be recreated.

Declaration order is free-form; position is tracked vec0-style with two
parallel arrays (`user_column_kinds[]`, `user_column_idxs[]`).

Reserved/rejected user column names: `distance` (hidden column), `rowid`.

`declare_vtab`:

```sql
CREATE TABLE x(
  <pk-or-nothing>, <user columns in declaration order>,
  distance hidden          -- reserved for KNN (decision 9)
) [WITHOUT ROWID]          -- text pk only
```

## Shadow tables

For `CREATE VIRTUAL TABLE t USING tg0(...)` with geometry columns g0..gN and
data columns d0..dM:

```sql
CREATE TABLE t_info(key TEXT PRIMARY KEY, value ANY);
  -- 'CREATE_VERSION' = SQLITE_TG_VERSION; future migration hook

CREATE TABLE t_content(
  rowid INTEGER PRIMARY KEY,      -- int pk aliases this; text pk maps via t_rowids
  geom00 BLOB NOT NULL, ... geomNN BLOB NOT NULL,   -- WKB
  data00, ... dataMM                                 -- no affinity; enforced in C
);

CREATE VIRTUAL TABLE t_rtree00 USING rtree(id, minX, maxX, minY, maxY);
... one per geometry column ...

-- text primary key only:
CREATE TABLE t_rowids(rowid INTEGER PRIMARY KEY AUTOINCREMENT,
                      id TEXT UNIQUE NOT NULL);
```

Numbered internal names (`geom00`, `data00`) avoid identifier-quoting issues;
user names exist only in `declare_vtab`. `xShadowName` claims: `info`,
`content`, `rowids`, `rtree%02d` + the rtree module's own `_node/_parent/_rowid`
suffixes. `xDestroy` drops all of them and reports failures.

Requires the rtree module, checked at create/connect like today
(`db_supports_rtree`).

## Query plans / idxStr encoding

vec0-style: `idxNum` unused, `idxStr` = 1 header char + fixed-width blocks per
consumed argv slot.

- Header (plan): `'1'` fullscan, `'2'` point (rowid/id EQ), `'3'` predicate,
  `'4'` reserved for KNN.
- Predicate plan blocks (4 chars): `[kind][geom-col ('A'+idx)][op][strategy]`
  where op identifies the tg predicate (intersects/touches/…) and strategy the
  rtree prefilter (overlap / contains / within / fullscan-for-disjoint).
- Pushdown blocks (ticket 05): `['&']['A'+data-col][op]['_']` mirroring vec0's
  metadata operator alphabet.

xBestIndex rules:

- A function constraint is only consumed when `iColumn` is a geometry column
  (fixes the aux-interception bug). Predicates on non-geometry columns and
  unconsumed extra predicates are left to SQLite (they run as plain scalar
  functions over returned WKB).
- Exactly one predicate consumed per scan; prefer a non-disjoint one.
- rowid EQ (or text-pk `id =`) → point plan, cost ~1.
- `colUsed` may be used to skip fetching unreferenced data columns (nice to
  have, not required).

**Float32 caveat (matters for correctness of within/coveredby):** the rtree
stores bboxes as float32 rounded *outward*, so a stored candidate bbox is a
superset of the true bbox. For the ⊆-strategy (`within`/`coveredby`), testing
`stored ⊆ query-bbox` can produce a **false negative** when outward rounding
pushes the stored bbox past a query edge. The query bbox must therefore be
rounded outward to float32 the same way (round min bounds down, max bounds up
to the nearest representable float) before comparison. The ⊇-strategy and
overlap are naturally conservative and need no adjustment. Ticket 04 must add
a regression test with a candidate whose true bbox lies within the query bbox
by less than one float32 ulp.

xFilter/xNext: rtree prefilter yields candidate ids → fetch content row →
parse geometry with the column's `indexing=` option (`tg_parse_wkb_ix`) →
exact tg predicate → emit or skip. Fullscan iterates `t_content` directly.
Point plan is a single content lookup (via `t_rowids` for text pk).

## Write semantics

- INSERT: for each geometry column — `geomValue()` parse (`TG_NONE`), NOT NULL
  check (11), strict type check (12), `tg_geom_rect()` → rtree row, WKB →
  content row (reuse input bytes verbatim when the input already is WKB —
  closes the old TODO). Data columns type-checked (NULL allowed). Set
  `*pRowid`. Text pk: insert into `t_rowids` first (UNIQUE violation surfaces
  as the constraint error), use the generated rowid everywhere.
- DELETE: remove content row + every rtree entry (+ rowids row). Propagate
  step errors (fixes return-OK-on-error).
- UPDATE: `sqlite3_value_nochange()`-aware per column; changed geometry
  columns update content + the corresponding rtree entry; rowid/PK change →
  `SQLITE_ERROR` "changing the primary key of a tg0 row is not supported".
- No xBegin/xSync/etc. — same posture as today; each statement's writes go
  through the underlying tables' own transactionality. Documented.

## Non-goals (this workstream)

- KNN implementation (surface reserved only — decision 9).
- Partition keys / per-value rtree sharding (decision 5).
- Parsed-geometry cache across queries.
- Quoted / unicode identifiers in the constructor.
- Serialized tg indexes on disk (tg has no such format).
- Z/M coordinate awareness in the index (bboxes are 2D; Z/M pass through WKB).

## Tickets / dependency order

| # | ticket | depends on |
|---|---|---|
| 01 | `01-parser.md` — tokenizer, column parser, declare_vtab | — |
| 02 | `02-schema.md` — shadow tables, xCreate/xConnect/xDestroy/xShadowName | 01 |
| 03 | `03-write-path.md` — INSERT/DELETE/UPDATE, enforcement, rowids | 02 |
| 04 | `04-read-path.md` — xBestIndex/xFilter/xNext/xColumn, all predicates | 03 |
| 05 | `05-pushdown.md` — data-column constraint pushdown (optional, can slip) | 04 |
| 06 | `06-polish.md` — xRename, error paths, OOM audit, valgrind | 04 |
| 07 | `07-docs-examples.md` — reference.yaml/docs/site/NOTES, migrate examples | 04 (06 ideally) |

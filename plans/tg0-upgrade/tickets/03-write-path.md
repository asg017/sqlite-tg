# 03 — Write path: INSERT / DELETE / UPDATE

`xUpdate` for the new layout (decisions 11, 12, 14, 15; "Write semantics" in
plan.md). Replaces sqlite-tg.c:3097-3234.

## Tasks

1. INSERT:
   - Geometry columns: `geomValue()` with `TG_NONE`; NULL/omitted →
     `SQLITE_CONSTRAINT` "<col>: geometry columns cannot be NULL"; typed
     columns enforce `tg_geom_typeof` exactly (decision 12, error names
     column + expected + actual via `tg_geom_type_string`); `tg_geom_rect()`
     → rtree insert; WKB → content. If the input value is already a WKB blob,
     store its bytes verbatim instead of re-serializing (closes the TODO at
     sqlite-tg.c:3192).
   - Data columns: type check against declared type (NULL allowed), bind
     verbatim.
   - Rowid/pk: explicit rowid or int pk honored; text pk → insert into
     `t_rowids` (UNIQUE NOT NULL violations surface naturally), use generated
     rowid for content + rtrees. **Set `*pRowid`** (fixes last_insert_rowid).
   - All-or-nothing: on any failure, no partial rows remain (rely on
     statement-level atomicity of the underlying tables; verify in tests).
2. DELETE: remove content + every rtree entry (+ rowids). Propagate step
   errors — fixes the return-OK-on-error bug (sqlite-tg.c:3120-3124).
3. UPDATE:
   - `sqlite3_value_nochange()`-aware per column.
   - Changed geometry column: same validation as INSERT, update content blob
     + that column's rtree entry (UPDATE on the rtree vtab).
   - Changed data column: type check + update.
   - argv[0] != argv[1] (rowid/pk change) → error "changing the primary key
     of a tg0 row is not supported".
4. Use a shared `tg0_set_error` helper (replace the unused
   `tg_vtab_set_error`, sqlite-tg.c:2667) everywhere; include
   `sqlite3_errmsg` for prepare/step failures.

## Acceptance

- Tests: round-trip WKT/WKB/GeoJSON/pointer inserts (typeof(geom col) =
  'blob', WKB-in stored byte-identical); NULL geometry rejected; wrong
  subtype rejected with exact message (snapshot); data-column type mismatch
  rejected; NULL data column ok; `last_insert_rowid()` correct; text pk
  insert/duplicate/select-by-id; DELETE removes rtree entries (query the
  rtree shadow directly); UPDATE of geometry moves the rtree bbox; UPDATE of
  data column with unchanged geometry does not touch rtrees
  (`sqlite3_value_nochange` path); pk-change UPDATE errors.
- `make test-memory` + `tests/test-valgrind.sh` clean on the new paths.

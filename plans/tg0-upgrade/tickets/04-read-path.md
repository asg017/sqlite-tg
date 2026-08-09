# 04 — Read path: xBestIndex / xFilter / xNext / xColumn, all 8 predicates

## Status: complete (52ee1b5)

Query planning + execution for the new layout (decisions 7, 8, 9, 13, 15;
"Query plans / idxStr encoding" in plan.md). Replaces
sqlite-tg.c:2822-3095.

## Tasks

1. `xFindFunction`: keep the 8 predicate overloads (TG0_FUNC_* 150-157),
   unchanged SQL-visible behavior as plain functions.
2. `xBestIndex`:
   - Only consume a function constraint whose `iColumn` is a geometry column
     (fixes the aux-interception bug at sqlite-tg.c:2831).
   - Plans: `'2'` point on rowid EQ / text-pk id EQ (cost ~1); `'3'`
     predicate (consume exactly one, prefer non-disjoint; extra predicates
     left unconsumed for SQLite to post-evaluate); `'1'` fullscan otherwise.
     `'4'` reserved for KNN — a `MATCH`/`distance` constraint today gets a
     clear "KNN is not supported yet" error rather than a crash.
   - idxStr encoding per plan.md; `needToFreeIdxStr` handled properly.
   - Remove the "only 1 predicate allowed" error (now: consume one, pass
     the rest).
3. `xFilter`/`xNext`:
   - Predicate plan: parse query geometry once; rtree prefilter per strategy
     table in plan.md (overlap / ⊇ / ⊆ / none-for-disjoint); candidates
     joined to `t_content`; per-candidate `tg_parse_wkb_ix` with the
     column's `indexing=` option; exact tg predicate; emit/skip.
   - **Float32 rounding**: for the ⊆ strategy, round the query bbox outward
     to float32 (min down, max up) before binding, so outward-rounded stored
     bboxes can't be false-negatived. See plan.md "Float32 caveat".
   - Point plan: single content lookup (via `t_rowids` for text pk).
   - Fullscan: iterate `t_content` directly (no rtree involvement).
   - Disjoint: fullscan + exact `tg_geom_disjoint` per row.
4. `xColumn`: pk/user columns from the content statement (WKB blobs for
   geometry — decision 13); `distance` returns NULL this release. Honor
   `colUsed` to skip unreferenced data columns if cheap.
5. `xRowid`: content rowid (works for text pk too — int64 rowid exists
   internally; SQLite won't call xRowid for WITHOUT ROWID vtabs).
6. Every prepare/step error reports `sqlite3_errmsg` (no more "prep error").
   Mid-scan step failure must return the error, not read as EOF (fixes
   tg0Next smell #19 from research).

## Acceptance

- All 8 predicates return correct results on a fixture with known
  relationships (points/lines/polygons in and out of each relation);
  disjoint verified against a manual scan.
- Two-predicate query works and returns the intersection of both filters.
- Predicate on a data column (e.g. `tg_intersects(name, ...)`) is NOT
  intercepted: it errors/behaves exactly as the scalar function would.
- `EXPLAIN QUERY PLAN` snapshots: fullscan, point (rowid EQ, text-pk EQ),
  one per predicate strategy; multi-geometry table picks the rtree of the
  referenced column.
- Float32 ulp regression test for within/coveredby (candidate true-bbox
  inside query bbox by < 1 float32 ulp still matches).
- Delete the old "not supported yet" predicate assertions in
  tests/test-loadable.py:1470-1513 (deliberate behavior change).
- `make test-loadable`, `make test-api` (snapshots regenerated), valgrind.

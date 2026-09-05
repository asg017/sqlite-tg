# 03 — KNN cursor: rtree node walk, best-first heap, plan `'4'`

The core ticket. Decisions 1, 2, 3, 4, 8, 9, 13.

## Tasks

1. **Node reader.** `tg0_rtree_node_read(cursor, nodeno, *out)`: prepared
   once per cursor as `SELECT data FROM "schema"."t_rtreeNN_node" WHERE
   nodeno = ?`; parse header (root: depth in bytes 0-1; all: cell count in
   bytes 2-3) and cells (8-byte BE int64, then 4 BE float32:
   minX, maxX, minY, maxY). Validate `4 + ncell * 24 <= blob length`,
   `depth <= 40`, root present; else `SQLITE_CORRUPT_VTAB` naming the
   shadow table. Cache node size from the root on first read.
2. **Heap.** Binary min-heap of `{double key; uint8 kind; int level;
   int64 id; double exact}` with `kind ∈ {NODE, LEAF, ROW}`; tie-break
   ROW < LEAF < NODE then id, so equal distances emit deterministically.
   Grows geometrically; OOM → `SQLITE_NOMEM`.
3. **xBestIndex.** Recognise `SQLITE_INDEX_CONSTRAINT_MATCH` on a geometry
   column → plan `'4'`, `argvIndex = 1`, `omit = 1`. Errors: MATCH on a
   non-geometry column; two MATCH terms; MATCH combined with a rowid/pk EQ
   (or just let the point plan win? — pick: error, simpler to document).
   Consume `ORDER BY distance ASC` (single term, column ==
   `tg0_column_distance_idx`) with `orderByConsumed = 1`. Cost: rtree
   traversal ≈ `log2(rows) + estimatedRows` content lookups; use
   `SQLITE_INDEX_CONSTRAINT_LIMIT` value for `estimatedRows` when present,
   else 25. Constraints on `distance` other than those in ticket 04 keep
   the current error until 04 lands.
4. **xFilter.** Parse the MATCH value with `geomValue()`; require
   `TG_POINT` (error otherwise, decision 1); geodesic columns validate the
   query point with `tg_lonlat_valid`. Prepare the node reader and the
   candidate fetch statement — the existing content SELECT shape with
   `WHERE c.rowid = ?1` (text pk: join `t_rowids`). Seed the heap with
   `NODE(1, depth)`; if the root has depth 0 treat its cells as leaves.
5. **Pop loop** (`tg0_knn_step`): NODE → read, for each cell push
   `NODE(child, level−1)` or `LEAF(rowid)` keyed by the metric's rect bound
   (cell coords cast to double); LEAF → step the candidate fetch, parse WKB
   with the column's `indexing=`, exact distance via
   `tg_geom_point_distance`, push `ROW(rowid, exact)` (skip if +∞ — open
   question 4), keep the fetched row's column values in the cursor slot
   only when it is emitted (re-fetch on emit is simpler than caching N
   candidate rows; measure — a rowid point lookup is ~1 µs); ROW → current
   row, return. Empty heap → `eof`.
6. **xColumn** `distance` → `sqlite3_result_double(exact)` on plan `'4'`,
   NULL otherwise. **xRowid** → emitted rowid. Text pk id comes from the
   candidate fetch join.
7. Tests (`tests/test-loadable.py`, "tg0 knn" section, snapshots):
   - 10-NN over a fixture of ~200 random world points, both metrics,
     compared row-for-row against a brute-force SQL/Python ranking;
   - ordering across the antimeridian and near a pole (geodesic);
   - polygon/line candidates (nearest timezone-style polygons; query inside
     a polygon → distance 0 first);
   - text-pk table, rowid-alias table, empty table, single-row table;
   - `EXPLAIN QUERY PLAN` snapshot showing no temp B-tree for
     `ORDER BY distance`, and one *with* a temp B-tree for `DESC`;
   - `LIMIT 1` visits few nodes: assert via a debug counter exposed through
     `SQLITE_TG_DEBUG` or by row count on a table where a fullscan would be
     obvious;
   - insert then query inside one transaction (node flush semantics);
   - `distance` is NULL on fullscan/point/predicate plans (existing tests).
   - valgrind/test-memory runs of the above.

## Acceptance

- The three query shapes at the top of plan.md return correct, nearest-first
  results with `LIMIT` short-circuiting (no full scan) on a 100k-point
  table (bench note in the PR using the LA buildings or overture example).
- `make test-loadable test-api test-memory` and `tests/test-valgrind.sh`
  clean.

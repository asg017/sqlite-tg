# 05 — Polish: corrupt-node defense, NaN/empty, OOM, error audit

## Tasks

1. Fuzz the node reader: truncated blobs, cell counts past the blob end,
   depth 65535, child nodeno 0 / missing / self-referential (cycle guard:
   level must strictly decrease; a node whose level is 0 but whose cells
   reference nodes is corrupt). All → `SQLITE_CORRUPT_VTAB`, never a crash
   or infinite loop. Build fixtures by hand-editing `t_rtree00_node` via
   SQL in the test.
2. NaN/inf handling in both metrics: bounds of NaN → +∞ so the heap stays
   ordered; verify with a planar table that was created before 02's checks
   (or by `UPDATE t_rtree00_node` in a test).
3. Empty geometries per open question 4; document.
4. OOM audit of the new region (heap growth, node blob copy, per-candidate
   parse) with `make test-memory`; valgrind run.
5. Error message audit: every new `tg_vtab_set_error` names the table and
   column; MATCH-not-a-point message shows the received type via
   `tg_geom_type_string`.
6. Cursor reuse: xFilter called twice on one cursor (correlated subquery)
   tears down the heap/statements; add a test with a correlated KNN
   subquery (`SELECT (SELECT name FROM places WHERE geom MATCH other.geom
   ORDER BY distance LIMIT 1) FROM other`) — the canonical "nearest X for
   each Y" join.
7. NOTES / CHANGELOG snippet.

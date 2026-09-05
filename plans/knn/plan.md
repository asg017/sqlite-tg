# tg0 KNN — nearest-neighbor queries on tg0 tables

Implement the KNN surface that the tg0 v2 rewrite reserved (tg0-upgrade
plan.md decision 9: hidden `distance` column, idxStr plan `'4'`). Target
query shapes:

```sql
-- the 10 places nearest a lon/lat
SELECT id, name, distance
FROM places
WHERE geom MATCH tg_point(:lng, :lat)
ORDER BY distance
LIMIT 10;

-- stream the closest places inside a bbox, nearest first
SELECT id, name, distance
FROM places
WHERE geom MATCH tg_point(:lng, :lat)
  AND tg_intersects(geom, :bbox_polygon)
ORDER BY distance;

-- everything within 5 km, nearest first
SELECT id, name, distance
FROM places
WHERE geom MATCH tg_point(:lng, :lat)
  AND distance <= 5000
ORDER BY distance;
```

Stacks on `asg017/tg0-upgrade` (worktree `../sqlite-tg-wts/tg0-upgrade`,
unmerged as of 2026-09-05); cut the branch from there. Tickets in
`plans/knn/tickets/*.md`, worked in order. Conventions as in the tg0 upgrade:
one branch per ticket, per user-visible change update `reference.yaml` →
`make docs` / `make docs-check`, NOTES checkbox, snapshot tests, run
`make test-loadable`, `make test-api`, `make test-memory`,
`tests/test-valgrind.sh`.

Status: **planned 2026-09-05; all five open questions answered by Alex the
same day with the recommendations below.** Implementation on branch
`asg017/knn` (worktree `../sqlite-tg-wts/knn`).

## Research findings

### What tg gives us

tg (vendored v0.4.0; upstream `main` checked 2026-09-05 — nothing newer) is
strictly planar/Cartesian. Relevant primitives:

| tg symbol | what | public? |
|---|---|---|
| `tg_point_distance_point` | Euclidean point↔point | in tg.c, **not** in tg.h |
| `tg_point_distance_segment` | Euclidean point↔segment (clamped projection) | in tg.c, not in tg.h |
| `tg_point_distance_rect`, `tg_rect_distance_rect` | Euclidean gap between boxes | in tg.c, not in tg.h |
| `tg_ring_nearest_segment`, `tg_line_nearest_segment` | best-first kNN over a ring/line's segments using its `TG_NATURAL`/`TG_YSTRIPES` index. **Caller supplies `rect_dist` and `seg_dist` callbacks**, so the metric is pluggable | public |
| `tg_line_length`, `tg_ring_area`, `tg_ring_perimeter` | planar measures | public (deliberately not wrapped — see api-design memory) |

There is **no** geometry↔geometry distance, no haversine, no geodesic
anything, and no "nearest" at the `tg_geom` level. Point-in-polygon,
intersection, and the segment indexes all treat edges as straight lines in
whatever coordinate space the numbers are in.

Consequence: every distance number in sqlite-tg has to be written by us. tg's
contribution is the segment-level best-first traversal, whose callback
design means our geodesic metric plugs straight in (the only contract is
`rect_dist(rect) <= seg_dist(seg)` for every segment inside `rect`).

### What SQLite's rtree gives us

- `sqlite3_rtree_query_callback()` implements exactly the best-first search
  we want (priority queue keyed on the callback's `rScore`, "smaller scores
  are processed first"; `rtreeSearchPointCompare` in rtree.c). **But it is
  not part of the loadable-extension API**: `sqlite3ext.h` has no rtree
  entries at all (checked vendored 3.49 and upstream), so `dist/tg0.dylib`
  loaded into python/node/the CLI cannot call it. Only static/WASM builds
  could. Rejected as the primary mechanism; dlsym tricks rejected as too
  fragile across Windows/static hosts.
- The rtree on-disk node format **is documented** in the rtree.c source
  header ("Database Format of R-Tree Tables"): `%_node(nodeno INTEGER
  PRIMARY KEY, data BLOB)`; root is always nodeno 1 and always exists;
  bytes 0-1 of the root = tree depth (big-endian), bytes 2-3 = cell count;
  cells are an 8-byte big-endian integer (leaf: rowid; interior: child
  nodeno) followed by `2*nDim` 4-byte big-endian float32 coordinates
  (minX, maxX, minY, maxY for our 2-D trees). Node size = root blob length
  (≤ 4 + 51×24 = 1228 bytes for 2-D). Dirty nodes are written back to
  `%_node` in `nodeRelease()` at the end of every rtree xUpdate, so reading
  `%_node` through SQL inside the same transaction sees the current tree.
  Stored bboxes are float32 rounded outward, so a cell bbox is a superset of
  the row's true bbox: any distance lower bound computed from it is still a
  valid lower bound for the row.
- `SQLITE_INDEX_CONSTRAINT_MATCH` already reaches tg0's xBestIndex for
  `geom MATCH x` (today it errors "KNN queries are not supported"); a
  vtab that consumes the MATCH constraint needs no `match()` overload.
  `SQLITE_INDEX_CONSTRAINT_LIMIT` (3.38+) is available as a hint only.

### Where planar math breaks on lon/lat data (the "earth is a sphere" audit)

These are the concrete failure modes that KNN inherits if we treat degrees as
Cartesian, and what the design does about each.

1. **Anisotropy: one degree of longitude shrinks with latitude.** At 60°N,
   1° lon ≈ 55.8 km but 1° lat ≈ 111 km. Planar ranking is wrong, not just
   mis-scaled: a place 1.0° east (55.8 km) ranks *behind* a place 0.9° north
   (100 km). This flips results in ordinary mid-latitude cities, not only at
   the poles. → default metric is great-circle (haversine) on a sphere.
2. **Units.** Planar `distance` on lon/lat is in degrees, which nobody can
   use for `distance <= 5000`. → geodesic returns meters.
3. **Antimeridian.** Points at lon 179.9 and −179.9 are 22 km apart;
   planar says 359.8°. The rtree cannot store a wrapping bbox, and tg
   treats a segment from 179 to −179 as crossing the whole planet. →
   the geodesic rect bound and point distance are wrap-aware (Δλ taken
   mod 360), so *points* on either side rank correctly with no storage
   change. *Geometries* that straddle ±180 stay wrong in tg (planar edge
   across the world) — documented limitation, data should be cut at the
   antimeridian as RFC 7946 §3.1.9 recommends.
4. **Poles.** All longitudes converge; a bbox touching lat ±90 has an
   edge of zero length. The rect lower bound must handle the "query longitude
   inside the rect's lon span → distance is along a meridian to the nearer
   parallel" case and clamp latitudes; naive clamp-then-haversine gives an
   *upper* bound, which silently breaks best-first ordering.
5. **Rect lower bound under a spherical metric is not clamp-then-haversine.**
   The nearest point of a lon/lat rectangle to an outside point lies on the
   nearer *meridian* edge but generally not at the clamped latitude (meridians
   are great circles; the perpendicular foot slides along them). Use the
   S2LatLngRect::GetDistance construction: inside → 0; query lon within the
   rect's lon span → angular distance to the nearer parallel (exact, along
   the meridian); otherwise → great-circle distance from the query to the
   nearer meridian edge treated as a great-circle arc (3-D vector
   point-to-arc), which is exact. Parallels are handled by clamping because
   for a constant-latitude line the closest point *is* at the clamped
   longitude (cos d is monotone in |Δλ|).
6. **What is a segment?** GeoJSON edges are straight in lon/lat; PostGIS
   `geography` treats edges as great-circle arcs. A great-circle arc bulges
   poleward outside its endpoints' bbox (≈ 200 m for a 100 km edge at 45°),
   which would break the bbox lower bounds *and* disagree with tg's planar
   point-in-polygon. → edges stay planar in lon/lat (consistent with every
   tg predicate and both indexes). The exact distance to a non-point
   candidate is the haversine from the query to the closest point on the
   candidate, where "closest point" is found in a locally scaled frame
   (lon scaled by cos(lat_q)); documented as an approximation whose error is
   second-order in edge length. Point candidates (the "places" use case) are
   exact.
7. **float32 rtree slop.** Stored bboxes are float32: ~1e-5° ≈ 1 m at
   |lon| ≤ 180. Ordering leaf entries by bbox bound alone can misorder rows
   within ~2 m for points and by far more for polygons (bbox gap vs true
   gap). → a refinement heap: a leaf is re-inserted with its *exact*
   distance after fetching the geometry, and a row is emitted only when its
   exact distance is ≤ every remaining bound. Exact streaming order,
   no top-k truncation.
8. **Sphere vs ellipsoid.** Haversine on a mean-radius sphere is within
   ~0.5 % of WGS84 geodesic distance — the same accuracy class as PostGIS
   `ST_DistanceSphere`. Fine for ranking and for "within 5 km";
   an ellipsoidal metric (Karney) is a possible later `metric=` value.
9. **Projected / game coordinates mislabeled as lon/lat.** Haversine on
   `x = 500000` is garbage. → `metric=` is a per-column declaration, and
   geodesic columns validate `|x| ≤ 180, |y| ≤ 90` on write
   (SQLITE_CONSTRAINT) and on the query point, so a mismatch surfaces at the
   first INSERT with a message naming the option.
10. **Non-point query geometries** need geometry↔geometry distance, which tg
    lacks (line↔line / poly↔poly minimum needs a segment-pair search).
    → v1: the MATCH argument must be a Point; anything else is a clear
    error. Candidates may be any of the 7 types.
11. **NaN / infinite coordinates** poison a priority queue (NaN compares
    false everywhere). Geodesic columns reject them via the range check;
    planar columns treat a NaN bound as +∞ (reject on write too — see
    ticket 05).
12. **Z/M** are ignored: distances are 2-D, as are the indexes. Unchanged
    from the rest of sqlite-tg.

## Decisions

Numbered for reference from tickets. Items marked **(open)** were
settled 2026-09-05 per "Open questions" below.

1. **Query shape: `geomcol MATCH <point>`; no new `tg_knn()` function.**
   The MATCH right-hand side is any geometry value `geomValue()` accepts
   (WKB/WKT/GeoJSON/pointer), so `MATCH tg_point(:lng, :lat)` and
   `MATCH 'POINT(-118.24 34.05)'` both work. It must parse to a Point
   (finding 10). Exactly one MATCH per query; MATCH on a non-geometry
   column is an error.
2. **Streaming, nearest-first, no `k`.** The cursor emits rows in exact
   ascending distance and never truncates, so `LIMIT` and any extra WHERE
   terms SQLite post-filters stay correct (tg0-upgrade decision 9). No
   `k = ?` constraint. `SQLITE_INDEX_CONSTRAINT_LIMIT`, when present, only
   feeds `estimatedRows`.
3. **`ORDER BY distance` is consumed** (`orderByConsumed = 1`) when the ORDER
   BY is exactly one ASC term on the hidden `distance` column. Any other
   ORDER BY (DESC, extra terms, none) is left to SQLite, which then sorts
   the full stream — documented: always write `ORDER BY distance` with a
   LIMIT.
4. **`distance` column** = the exact distance of the current row under the
   column's metric: meters (geodesic) or coordinate units (planar). NULL on
   every non-KNN plan (as today). Constraints on `distance` without a MATCH
   remain an error.
5. **Metric is a per-geometry-column constructor option**
   `metric=geodesic|planar`, **default `geodesic`** (open: name + default).
   Rationale: the metric is a property of the data, not the query; every
   sqlite-tg example and the WASM playground datasets are lon/lat; and
   shipping a planar-only default would repeat the planar-measurement
   mistake Alex already rejected. Geodesic columns validate coordinate
   ranges on INSERT/UPDATE and on the query point (finding 9). Option is
   re-parsed from the CREATE VIRTUAL TABLE args on xConnect like
   `indexing=`; nothing new is stored.
6. **Geodesic metric = haversine great-circle distance on a sphere of
   radius 6 371 008.8 m** (IUGG mean radius; same constant as turf.js and
   PostGIS's spherical functions). Exact for point candidates. For
   line/polygon candidates the closest point on each planar lon/lat edge is
   found in a frame scaled by cos(lat_query), then haversine to it
   (finding 6). A polygon that contains the query point is at distance 0
   (via `tg_geom_intersects_xy`). Multi*/collections: minimum over members.
   Empty geometries: +∞ (never emitted? — emit last; open question 4).
7. **Planar metric** = Euclidean in coordinate units; point↔segment via the
   same clamped-projection formula tg uses; rect bound = Euclidean gap.
   Re-implement the 5-line helpers locally rather than prototyping tg's
   non-public `tg_point_distance_*` symbols.
8. **Traversal: tg0 walks the rtree's `%_node` blobs itself** (finding: rtree
   query callback unavailable to loadable extensions). One best-first heap
   per cursor holding three entry kinds — interior node (bound), leaf entry
   (bound, rowid), refined row (exact distance, rowid). Pop loop: node →
   read blob, push children with bounds; leaf → fetch content row by rowid,
   parse WKB with the column's `indexing=`, compute exact distance, apply
   folded exact predicate (decision 10), push refined; refined → emit.
   Correctness argument: every entry's key is a lower bound on the distance
   of everything beneath it, and a refined key is exact, so the popped
   refined row is ≤ all remaining rows. Nodes are read with one prepared
   `SELECT data FROM "t_rtreeNN_node" WHERE nodeno = ?` per cursor (or
   `sqlite3_blob_open`/`reopen`, implementer's call). Node parsing is
   defensive: cell count × cell size must fit the blob, depth ≤ 40, else
   `SQLITE_CORRUPT_VTAB` with a message naming the shadow table.
   Alternatives considered: (a) `sqlite3_rtree_query_callback` — rejected
   above; (b) expanding-ring range queries with re-sorting per ring —
   dependency-free but O(n) memory in late rings and awkward ring sizing
   under a geodesic metric; kept as a fallback if node walking proves
   unworkable.
9. **Bounds use the same metric as the exact distance.** Geodesic: the
   S2-style lon/lat rect distance (finding 5), wrap-aware in longitude.
   Planar: Euclidean rect gap. Both are also the `rect_dist` callback handed
   to `tg_ring_nearest_segment` / `tg_line_nearest_segment` for big
   candidates, with `seg_dist` = the matching point↔segment distance.
10. **Predicate folding.** One tg predicate on the *same* geometry column
    (`tg_intersects(geom, q)` etc.) is consumed together with the MATCH and
    applied inside the traversal: prune interior/leaf cells by the
    predicate's rtree strategy (overlap → skip non-overlapping cells;
    within/coveredby → skip non-overlapping cells, exact ⊆ check at leaf;
    contains/covers → skip cells not ⊇ the query bbox; disjoint → no
    pruning), then the exact predicate on the parsed candidate. This is what
    makes "closest places inside this bbox" not scan the planet. Predicates
    on other geometry columns stay unconsumed (SQLite post-filters,
    streaming order preserved).
11. **Distance pruning.** `distance < ?` / `distance <= ?` is consumed with
    `omit = 0` as a maximum: traversal stops when the heap minimum exceeds
    it. `>`, `>=`, `=`, `!=` on distance stay unconsumed. `distance` in the
    query bound is compared after the same float32 outward rounding logic
    is irrelevant here (bounds are already conservative).
12. **Data-column pushdown** (tg0-upgrade decision 10) also applies to the
    KNN candidate fetch: `'&'` blocks append `AND dataNN op ?` to the
    per-rowid content SELECT, skipping WKB parse + distance for rejected
    rows. Same affinity rules as the existing pushdown code.
13. **idxStr for the KNN plan:** header `'4'`, then
    `['K']['A'+geom-col][metric]` (metric letter `G`/`P`, informational),
    optional predicate block `['P'][geom-col][op][strategy]` (same alphabet
    as plan `'3'`), optional `['D'][op-letter]['_']['_']` distance-max
    block, then `'&'` pushdown blocks. argv order: MATCH value, then
    predicate query geometry, then distance max, then pushdown values.
14. **Scalar `tg_distance_sphere(a, b)`** — open question 3. If accepted it
    reuses the exact-distance kernel (one argument must be a Point, error
    otherwise) and gives users the same number outside tg0.

## Open questions for Alex (all answered 2026-09-05: go with the recommendation)

1. **Option name and default.** Recommendation: `metric=geodesic|planar`,
   default `geodesic`, with write-time lon/lat range validation on geodesic
   columns. Alternatives: `coords=lonlat|planar`, or default `planar` with
   no validation (rejected above — misleads on the common case).
2. **Non-point candidates in v1** (lines/polygons under the scaled-planar
   closest-point approximation, decision 6) vs. **points only** with a
   clear error for other types. Recommendation: include them — tg's
   `*_nearest_segment` makes it cheap, "nearest building/timezone" is a
   real query, and the approximation is documented and second-order.
3. **Expose `tg_distance_sphere(a, b)`?** It passes the api-design bar (no
   builtin composition can compute it; geodesic-correct), but it is partial
   (point vs any). Recommendation: yes, as the last ticket, droppable.
4. **Empty geometries** in a KNN scan: emit last with `distance = NULL`, or
   never emit? Recommendation: never emit (an empty geometry has no nearest
   point; matches PostGIS `<->` behaviour of sorting NULL last only by
   accident).
5. **Break the tg0 write path for existing geodesic-default tables?** Range
   validation is a new constraint on columns that previously accepted any
   numbers. tg0 is alpha and unmerged, so recommendation: yes, no migration.

## Non-goals

- Geometry↔geometry distance (non-point MATCH argument, non-point scalar
  pairs). Needs a segment-pair search tg does not have.
- Ellipsoidal (Karney/Vincenty) distances; `metric=ellipsoid` is a natural
  later value.
- Geodesic edges (great-circle interpolation) — conflicts with tg's planar
  model (finding 6).
- Correct handling of geometries that straddle the antimeridian.
- `k = ?` syntax, DESC ordering, farthest-neighbor.
- Using `sqlite3_rtree_query_callback` in static/WASM builds as a fast path.

## Tickets / dependency order

| # | ticket | depends on |
|---|---|---|
| 01 | `01-distance-kernel.md` — planar + geodesic point↔(point, segment, rect, geom) kernel | — |
| 02 | `02-metric-option.md` — `metric=` constructor option, write-time validation | 01 (only for the range check helper) |
| 03 | `03-knn-cursor.md` — node reader, best-first heap, plan `'4'`, xFilter/xNext/xColumn, ORDER BY consumption | 01, 02 |
| 04 | `04-pruning.md` — predicate folding, distance max, pushdown in candidate fetch, LIMIT hint | 03 |
| 05 | `05-polish.md` — corrupt-node defense, NaN/empty handling, OOM/valgrind, error audit | 03 |
| 06 | `06-docs-examples.md` — reference.yaml/docs/NOTES/site2, examples | 03 (04 ideally) |
| 07 | `07-scalar-distance.md` — optional `tg_distance_sphere()` | 01 |

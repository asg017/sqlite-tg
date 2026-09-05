# 01 — Distance kernel: planar + geodesic, point ↔ {point, segment, rect, geom}

Pure C, no vtab changes. Everything KNN ranks with lives here, so the rtree
traversal (03), tg's segment traversal, and the optional scalar (07) all
share one definition of "distance". Decisions 6, 7, 9.

## Tasks

1. New `#pragma region distance` in `sqlite-tg.c` with a metric enum
   `TG_METRIC_GEODESIC` / `TG_METRIC_PLANAR` and a small vtable-ish struct
   `tg_metric` (`point_point`, `point_segment`, `point_rect`) so callers
   never branch on the enum themselves.
2. Planar: Euclidean point↔point; point↔segment via clamped projection (same
   formula as tg's `tg_point_distance_segment`, re-implemented — do not
   prototype tg's non-public symbols); point↔rect = Euclidean gap.
3. Geodesic (all inputs lon/lat degrees, output meters, `R = 6371008.8`):
   - `haversine(a, b)`.
   - point↔segment: build a local frame at the query
     (`x = (lon - lon_q) * cos(lat_q)`, `y = lat - lat_q`, longitude
     difference wrapped to (−180, 180]), clamped projection in that frame,
     map the parameter `t` back to the original segment, haversine to it.
   - point↔rect lower bound (S2LatLngRect::GetDistance construction):
     inside → 0; lon_q within `[minX, maxX]` (wrap-aware) → angular distance
     to the nearer parallel `min(|lat_q − minY|, |lat_q − maxY|)` after
     clamping; otherwise → great-circle distance from the query to the
     nearer meridian edge (choose by wrapped Δλ) as a 3-D point-to-arc
     computation (unit vectors, arc normal `n = a × b`; if the foot lies
     within the arc, `asin(|n̂·q|)`, else min of the endpoint distances).
     Degenerate rects (point bboxes, zero-height at a pole) must fall
     out naturally — add tests for them.
   - Range check helper `tg_lonlat_valid(x, y)` (`|x| ≤ 180`, `|y| ≤ 90`,
     finite) used by 02 and 03.
4. `tg_geom_point_distance(metric, query_point, geom, *out)` over all 7
   types: Point → point↔point; LineString → `tg_line_nearest_segment` with
   `rect_dist`/`seg_dist` from the metric, stop after the first iterated
   segment (nearest); Polygon → 0 if `tg_geom_intersects_xy`, else minimum
   over exterior + holes via `tg_ring_nearest_segment`; Multi*/Collection →
   min over members; empty → `INFINITY` (decision 6 / open question 4).
   Return false on tg OOM (`*_nearest_segment` returns false).
5. Unit tests. There is no C unit harness; test through SQL instead: land
   ticket 07's `tg_distance_sphere()` early *if* open question 3 is yes,
   otherwise add a temporary `tg0_debug_distance(metric, point, geom)`
   function guarded by `#ifdef SQLITE_TG_DEBUG` and exercise it from
   `tests/test-loadable.py` against a Python haversine reference:
   - known pairs (LA↔NYC ≈ 3 936 km, 1° lat at equator ≈ 111.19 km);
   - antimeridian pair (179.9, 0)↔(−179.9, 0) ≈ 22.2 km;
   - polar rect cases and the "query lon inside rect lon span" case;
   - rect bound ≤ exact for 10k random (point, rect) pairs including
     wrap-around and pole-touching rects (property test);
   - point↔segment ≤ haversine to either endpoint; inside-polygon → 0.

## Acceptance

- Property test passes: `rect_bound(q, bbox(g)) <= exact(q, g)` for random
  geometries of every type under both metrics.
- Haversine values match the Python reference to 1e-6 relative.
- No new public SQL surface unless 07 is accepted.

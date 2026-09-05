# KNN benchmark: tg0 `MATCH` vs other local nearest-neighbor options

Nearest-neighbor queries ("the k places nearest this lon/lat", "everything
within 2 km") on [Overture Maps](https://overturemaps.org) places, against
the other ways you could do it locally without a server round-trip... plus
PostGIS as the reference implementation.

## Datasets

Overture places (points), release `2026-08-19.0`, pulled straight from the
public S3 bucket with DuckDB (`download-places.sql`, `download-places-us.sql`):

| dataset | bbox | rows | parquet |
|---|---|---|---|
| `ca` | California (-124.6 … -114.0, 32.4 … 42.1) | 2,024,512 | 87 MB |
| `us` | contiguous US (-125 … -66.9, 24.5 … 49.5) | 17,898,097 | 781 MB |

Every system loads the same rows with the same sequential ids (assigned in
Overture GERS id order), so results are compared by id.

## Systems

- **tg0** — sqlite-tg's `tg0` table, `geom match tg_point(x, y) order by
  distance limit k`; a best-first walk of the bbox R*Tree with exact
  great-circle refinement, all inside SQLite.
- **spatialite** — SpatiaLite 5.1's `KNN2` virtual table over its R*Tree
  index. It is a radius search that sorts what it finds: it needs a start
  radius in degrees (0.001 here) and `expand = 1` to keep doubling it until
  `max_items` rows are found. Two quirks found while wiring it up: the
  constraints must be written with `radius` *before* `max_items` or it
  silently returns nothing, and ranking is by planar CRS distance
  (`distance_crs`) with geodesic meters reported alongside.
- **sqlite-rtree** — what you hand-roll without tg0: a plain SQLite R*Tree
  plus an expanding-box search in Python (start at ~200 m, quadruple until
  k rows are inside, re-query with a box that provably covers the k-th
  geodesic distance, rank the candidates by haversine in numpy).
- **postgis** — PostGIS 3.5 `geography` column with a GiST index,
  `order by geom <-> point limit k` (index-driven KNN, spheroidal distances).
  Local Postgres.app 17; per-query latency includes the socket round trip.
- **duckdb** — DuckDB 1.5 spatial without an index: `order by
  ST_Distance_Sphere(geom, point) limit k`, a vectorized brute-force top-k
  over every row (`SET geometry_always_xy = true`, or it reads [lat, lon]).
- **duckdb-rtree** — DuckDB spatial's R-tree index, which accelerates
  `ST_Within(geom, box)` but cannot order, driven by the same expanding-box
  search as `sqlite-rtree`.
- **kdtree-xyz** — in-memory scipy `cKDTree` on unit-sphere 3-D vectors:
  chord distance is monotone in great-circle distance, so it is exact
  geodesic KNN for points. No on-disk index; rebuilt from the parquet on
  every open.
- **kdtree-degrees** — the same `cKDTree` on raw lon/lat degrees, i.e. the
  planar-on-degrees mistake. Fast and wrong away from the equator; included
  to measure how often it changes the answer.
- **balltree-haversine** — in-memory scikit-learn `BallTree` with the
  haversine metric.
- **brute-numpy** — the correctness reference: haversine to every row in
  numpy, `argpartition` top-k.

## Method

100 query points per dataset: 80 are real places with ~1 km of Gaussian
jitter (dense areas), 20 are uniform over the bbox (deserts, ocean
margins — the awkward case for expanding-box searches). k ∈ {1, 10, 100,
1000} plus a 2 km radius query where the system has a natural form for it.
Each system gets a warm-up pass over every query point at k = 1000, then
each query is timed individually from Python (`time.perf_counter` around
the driver call, so driver overhead — roughly 20–50 µs for sqlite3, more
for psycopg — is included for everyone). SQLite-based systems get a 256 MB
page cache; PostgreSQL and DuckDB use their own buffer pools. Everything is
warm and single-threaded on an M4 Pro.

Correctness: a result counts as correct when the haversine distances of
the ids it returned (recomputed in numpy) equal the reference's sorted
distances to 1e-9 — so ties at the k-th distance (Overture has many
co-located places) and sphere-vs-spheroid reporting differences do not
count as errors, but returning a farther place does. "order" additionally
requires the rows to come back nearest-first.

```sh
./run.sh build --dataset ca      # loads every system (drop --systems to pick)
./run.sh run --dataset ca        # times the queries, writes results-ca.json
./run.sh report --dataset ca     # renders results-ca.md
```

## Results

See `results-ca.md` and `results-us.md` (generated), summarized below.

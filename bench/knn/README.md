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

Data lives in `data.noindex/` — the suffix keeps Spotlight from indexing
gigabytes of fresh database files, which pinned a core and the disk while
the US numbers were first taken.

```sh
./run.sh build --dataset ca      # loads every system (drop --systems to pick)
./run.sh run --dataset ca        # times the queries, writes results-ca.json
./run.sh report --dataset ca     # renders results-ca.md
```

## Results

Generated tables (`results-ca.md`, `results-us.md`) are inlined below.
Headline, 17.9M rows, warm, median per query:

- **tg0 is the fastest on-disk option at every k** — 0.06 ms (k=1),
  0.1 ms (k=10), 0.35 ms (k=100), 3.1 ms (k=1000), 2.2 ms for a 2 km
  radius — and the only one whose tail stays flat (p95 within 10% of the
  median). It goes from 2.0M to 17.9M rows without slowing down: the walk
  touches O(log n) nodes plus one content lookup per emitted row.
- **PostGIS `<->`** on a `geography` GiST index is the reference for
  correctness and is 5–80 ms here; part of that is the socket round trip
  and the 3.4 GB table against a default 128 MB `shared_buffers`, so read
  it as "a server-grade answer" rather than a ceiling.
- **The hand-rolled R*Tree + expanding box** (`sqlite-rtree`,
  `duckdb-rtree`) has a good median but a bad tail: 170–260 ms p95 at
  k=1000, because the box either overshoots (thousands of candidates to
  rank in Python) or has to be re-queried; the uniform "desert" query
  points are what push it.
- **SpatiaLite KNN2** answers a different question — a radius search
  ranked by planar degrees — so 10–15% of its k=10 answers and ~80% of its
  k=1000 answers differ from the geodesic truth, and its tail is the worst
  of the indexed systems (146–390 ms p95 at k=1000).
- **DuckDB without an index** is a flat 20 ms (2M rows) / 170 ms (17.9M)
  per query: vectorized brute force scales with the table, not with k.
- **In-memory trees win on raw latency** — a scipy KD-tree on unit-sphere
  vectors answers k=10 in 16 µs — but hold 17.9M points in RAM, rebuild
  on every process start (30–60 s), and are exact only for points. The
  same KD-tree on raw degrees is wrong for 62% of k=10 queries and every
  k=100 query: the planar-on-lon/lat mistake is not a corner case.
- Build cost: the first US load took 30 min against 17 min for a plain
  R*Tree, because tg0's write path re-prepared its shadow-table statements
  for every row. With the statements cached on the vtab (vec0-style) the
  California load went from 46 s to 33 s (plain R*Tree: 29 s) and an
  in-memory load of 600k points is at parity with a hand-written content
  table + rtree (6.1 s vs 6.2 s); what is left is the rtree's own insert
  cost. PostGIS (COPY + a bulk GiST build) and DuckDB (no index) load
  faster than any row-at-a-time SQLite path.

Numbers are single-machine (M4 Pro, macOS, APFS SSD), one process,
Python driver overhead included; treat differences under ~30% as noise.

### us: 17,898,097 Overture places

| system | build | size | k=1 | k=10 | k=100 | k=1000 | radius 2 km | correct (k=10) |
|---|---|---|---|---|---|---|---|---|
| tg0 | 1816 s | 1985 MB | 0.06 ms | 0.10 ms | 0.35 ms | 3.07 ms | 2.24 ms | 100% sets, 100% order |
| spatialite | 436 s | 2724 MB | 0.38 ms | 0.57 ms | 3.62 ms | 30.97 ms | — | 90% sets, 82% order |
| sqlite-rtree | 1041 s | 1912 MB | 0.57 ms | 1.04 ms | 3.82 ms | 18.80 ms | 3.10 ms | 100% sets, 100% order |
| postgis | 173 s | 3374 MB | 13.33 ms | 4.75 ms | 12.65 ms | 79.49 ms | 44.41 ms | 100% sets, 100% order |
| duckdb | 10 s | 657 MB | 159.92 ms | 166.18 ms | 168.36 ms | 171.76 ms | 168.80 ms | 100% sets, 100% order |
| duckdb-rtree | 11 s | 1103 MB | 0.70 ms | 1.44 ms | 4.90 ms | 25.21 ms | 3.99 ms | 100% sets, 100% order |
| kdtree-xyz | 0 s | in-memory | 0.01 ms | 0.02 ms | 0.04 ms | 0.36 ms | 0.10 ms | 100% sets, 100% order |
| kdtree-degrees | 0 s | in-memory | 0.01 ms | 0.03 ms | 0.27 ms | 2.71 ms | — | 38% sets, 4% order |
| balltree-haversine | 0 s | in-memory | 0.14 ms | 0.18 ms | 0.41 ms | 1.79 ms | — | 100% sets, 100% order |
| brute-numpy | 0 s | in-memory | 333.74 ms | 441.65 ms | 438.37 ms | 442.54 ms | 316.56 ms | 100% sets, 100% order |

Tail latency (p95), same runs:

| system | k=1 p95 | k=10 p95 | k=100 p95 | k=1000 p95 | radius p95 |
|---|---|---|---|---|---|
| tg0 | 0.11 ms | 0.16 ms | 0.43 ms | 3.31 ms | 126.85 ms |
| spatialite | 1.15 ms | 3.57 ms | 19.02 ms | 145.89 ms | — |
| sqlite-rtree | 3.34 ms | 14.91 ms | 25.24 ms | 169.84 ms | 21.85 ms |
| postgis | 34.92 ms | 13.32 ms | 33.86 ms | 146.46 ms | 292.45 ms |
| duckdb | 178.61 ms | 185.25 ms | 190.58 ms | 199.01 ms | 194.35 ms |
| duckdb-rtree | 5.09 ms | 18.65 ms | 34.84 ms | 261.43 ms | 26.89 ms |
| kdtree-xyz | 0.02 ms | 0.02 ms | 0.07 ms | 0.45 ms | 0.65 ms |
| kdtree-degrees | 0.01 ms | 0.04 ms | 0.30 ms | 2.81 ms | — |
| balltree-haversine | 0.30 ms | 0.41 ms | 0.89 ms | 3.47 ms | — |
| brute-numpy | 359.52 ms | 494.67 ms | 483.46 ms | 492.64 ms | 345.00 ms |

Median latency per query over 100 query points (80 near real places, 20 uniform over the bbox), warm, Python driver overhead included for every system. Correctness = share of queries whose top-10 id set / order equals the numpy brute-force haversine reference.


### ca: 2,024,512 Overture places

| system | build | size | k=1 | k=10 | k=100 | k=1000 | radius 2 km | correct (k=10) |
|---|---|---|---|---|---|---|---|---|
| tg0 | 47 s | 219 MB | 0.06 ms | 0.09 ms | 0.30 ms | 2.28 ms | 2.44 ms | 100% sets, 100% order |
| spatialite | 39 s | 309 MB | 0.20 ms | 0.55 ms | 2.44 ms | 24.30 ms | — | 85% sets, 77% order |
| sqlite-rtree | 29 s | 211 MB | 0.09 ms | 0.21 ms | 0.77 ms | 6.21 ms | 2.32 ms | 100% sets, 100% order |
| postgis | 9 s | 382 MB | 0.35 ms | 0.38 ms | 0.70 ms | 2.92 ms | 3.59 ms | 100% sets, 100% order |
| duckdb | 1 s | 69 MB | 19.87 ms | 20.84 ms | 19.69 ms | 20.37 ms | 20.29 ms | 100% sets, 100% order |
| duckdb-rtree | 1 s | 119 MB | 0.85 ms | 1.26 ms | 3.42 ms | 26.29 ms | 8.22 ms | 100% sets, 100% order |
| kdtree-xyz | 0 s | in-memory | 0.01 ms | 0.02 ms | 0.05 ms | 0.32 ms | 0.12 ms | 100% sets, 100% order |
| kdtree-degrees | 0 s | in-memory | 0.01 ms | 0.03 ms | 0.26 ms | 2.66 ms | — | 33% sets, 3% order |
| balltree-haversine | 0 s | in-memory | 0.08 ms | 0.09 ms | 0.24 ms | 1.07 ms | — | 100% sets, 100% order |
| brute-numpy | 0 s | in-memory | 33.44 ms | 44.80 ms | 44.92 ms | 45.32 ms | 31.28 ms | 100% sets, 100% order |

Tail latency (p95), same runs:

| system | k=1 p95 | k=10 p95 | k=100 p95 | k=1000 p95 | radius p95 |
|---|---|---|---|---|---|
| tg0 | 0.09 ms | 0.13 ms | 0.37 ms | 2.45 ms | 21.56 ms |
| spatialite | 1.23 ms | 2.14 ms | 12.31 ms | 371.38 ms | — |
| sqlite-rtree | 0.78 ms | 4.44 ms | 28.84 ms | 165.38 ms | 18.38 ms |
| postgis | 0.94 ms | 1.00 ms | 1.32 ms | 4.13 ms | 32.70 ms |
| duckdb | 20.50 ms | 22.74 ms | 20.32 ms | 22.03 ms | 21.52 ms |
| duckdb-rtree | 5.12 ms | 13.96 ms | 103.91 ms | 600.62 ms | 67.73 ms |
| kdtree-xyz | 0.02 ms | 0.03 ms | 0.10 ms | 0.40 ms | 1.18 ms |
| kdtree-degrees | 0.02 ms | 0.04 ms | 0.29 ms | 2.75 ms | — |
| balltree-haversine | 0.15 ms | 0.17 ms | 0.42 ms | 1.82 ms | — |
| brute-numpy | 39.09 ms | 51.31 ms | 50.20 ms | 50.20 ms | 33.39 ms |

Median latency per query over 100 query points (80 near real places, 20 uniform over the bbox), warm, Python driver overhead included for every system. Correctness = share of queries whose top-10 id set / order equals the numpy brute-force haversine reference.


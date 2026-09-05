### ca: 2,024,512 Overture places

| system | build | size | k=1 | k=10 | k=100 | k=1000 | radius 2 km | correct (k=10) |
|---|---|---|---|---|---|---|---|---|
| tg0 | 47 s | 219 MB | 0.06 ms | 0.08 ms | 0.29 ms | 2.28 ms | 2.66 ms | 100% sets, 100% order |
| spatialite | 39 s | 309 MB | 0.30 ms | 0.46 ms | 2.48 ms | 25.38 ms | — | 85% sets, 77% order |
| sqlite-rtree | 29 s | 211 MB | 0.09 ms | 0.21 ms | 0.73 ms | 6.14 ms | 2.44 ms | 100% sets, 100% order |
| postgis | 9 s | 382 MB | 0.35 ms | 0.42 ms | 0.83 ms | 3.13 ms | 3.56 ms | 100% sets, 100% order |
| duckdb | 1 s | 69 MB | 19.92 ms | 20.72 ms | 19.72 ms | 20.29 ms | 20.36 ms | 100% sets, 100% order |
| duckdb-rtree | 1 s | 119 MB | 0.66 ms | 1.28 ms | 3.37 ms | 28.49 ms | 8.56 ms | 100% sets, 100% order |
| kdtree-xyz | 0 s | in-memory | 0.01 ms | 0.02 ms | 0.04 ms | 0.29 ms | 0.22 ms | 100% sets, 100% order |
| kdtree-degrees | 0 s | in-memory | 0.01 ms | 0.03 ms | 0.27 ms | 2.62 ms | — | 33% sets, 3% order |
| balltree-haversine | 0 s | in-memory | 0.08 ms | 0.09 ms | 0.21 ms | 1.02 ms | — | 100% sets, 100% order |
| brute-numpy | 0 s | in-memory | 30.28 ms | 41.97 ms | 42.73 ms | 43.00 ms | 29.64 ms | 100% sets, 100% order |

Tail latency (p95), same runs:

| system | k=1 p95 | k=10 p95 | k=100 p95 | k=1000 p95 | radius p95 |
|---|---|---|---|---|---|
| tg0 | 0.09 ms | 0.11 ms | 0.36 ms | 2.60 ms | 25.53 ms |
| spatialite | 5.89 ms | 6.68 ms | 14.06 ms | 390.96 ms | — |
| sqlite-rtree | 0.77 ms | 3.67 ms | 27.44 ms | 160.11 ms | 18.61 ms |
| postgis | 1.05 ms | 1.04 ms | 2.51 ms | 4.49 ms | 29.41 ms |
| duckdb | 20.93 ms | 23.13 ms | 20.14 ms | 22.89 ms | 21.35 ms |
| duckdb-rtree | 2.80 ms | 15.54 ms | 104.89 ms | 617.38 ms | 62.49 ms |
| kdtree-xyz | 0.02 ms | 0.03 ms | 0.06 ms | 0.37 ms | 1.56 ms |
| kdtree-degrees | 0.02 ms | 0.04 ms | 0.28 ms | 3.81 ms | — |
| balltree-haversine | 0.14 ms | 0.16 ms | 0.37 ms | 1.89 ms | — |
| brute-numpy | 32.52 ms | 48.68 ms | 48.40 ms | 51.23 ms | 32.70 ms |

Median latency per query over 100 query points (80 near real places, 20 uniform over the bbox), warm, Python driver overhead included for every system. Correctness = share of queries whose top-10 id set / order equals the numpy brute-force haversine reference.


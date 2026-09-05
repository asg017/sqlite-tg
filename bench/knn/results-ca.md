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


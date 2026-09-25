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


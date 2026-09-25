sqlite3 base.db < build-base.sql

```
$ rm tmp-rtree.db; sqlite3 tmp-rtree.db < build-rtree.sql
Run Time: real 0.002 user 0.000743 sys 0.000965
Run Time: real 68.179 user 41.701366 sys 21.930421
```

```
$ rm tmp-tg.db; sqlite3 tmp-tg.db < build-tg.sql
Run Time: real 0.002 user 0.000442 sys 0.001033
Run Time: real 0.002 user 0.000602 sys 0.000875
Run Time: real 34.126 user 21.786830 sys 9.623461
Run Time: real 6.741 user 0.991173 sys 4.260120
Run Time: real 0.001 user 0.000010 sys 0.000001
Run Time: real 27.689 user 21.025726 sys 4.898099
Run Time: real 0.646 user 0.000424 sys 0.435493
```

```
$ sqlite3 tmp-rtree.db < query-rtree.sql
1152
Run Time: real 0.000 user 0.000561 sys 0.000269
```

```
sqlite3 tmp-tg.db < query-tg.sql
463
Run Time: real 1.374 user 1.265926 sys 0.104428
```

Nearest-neighbor queries on the same table (`knn-tg.sql`): the 10 nearest
footprints to a point, and everything within 250 m, both served from the
bbox R-Tree walk rather than a scan.

```
sqlite3 tmp-tg.db < knn-tg.sql
```

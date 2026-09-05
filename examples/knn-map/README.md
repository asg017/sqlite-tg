# KNN map — sqlite-tg in the browser

A point-and-click map for `tg0` nearest-neighbor queries, running the
sqlite-tg WASM build entirely in the browser.

```sh
make wasm                                   # dist/.wasm/sqlite3.{mjs,wasm} with sqlite-tg linked in
uv run --with duckdb python examples/knn-map/build-db.py   # data/places-sf.db from bench/knn's parquet
python3 -m http.server 8080                 # from the repo root
open http://localhost:8080/examples/knn-map/
```

Click anywhere: the sidebar shows the generated SQL, `EXPLAIN QUERY PLAN`,
the time the query took inside WASM, and the rows; the map shows numbered
results, a circle at the k-th distance (everything inside it was returned)
and, with a radius, the `distance <= r` circle.

Controls: k (LIMIT), a `distance <= meters` radius, "restrict to map view"
(a `tg_intersects` bbox predicate folded into the walk), a free-form extra
WHERE clause (e.g. `category = 'restaurant'`), and ORDER BY on/off. The
cross-checks run the same query the slow way: a full scan ranked by
`tg_distance_sphere()` (must produce identical distances) and a ranking by
Euclidean distance in degrees, which highlights the rows the planar shortcut
gets wrong.

Any SQLite file with a `tg0` table can be loaded through the file picker;
the bundled `places-sf.db` is 59k Overture Maps places in San Francisco.

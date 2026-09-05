#!/usr/bin/env python3
"""KNN benchmark: sqlite-tg's tg0 MATCH against other local nearest-neighbor
options, on Overture Maps places (points).

    uv run --with duckdb --with scipy --with scikit-learn --with numpy \
        --with "psycopg[binary]" bench.py build --dataset ca
    ... bench.py run --dataset ca
    ... bench.py report --dataset ca

Every system loads the same parquet file (see download-places.sql) with a
sequential integer id, so results are compared by id against a numpy
brute-force haversine reference.
"""
import argparse
import json
import math
import os
import sqlite3
import struct
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
DIST = HERE.parent.parent / "dist"
TG0_EXT = str(DIST / "tg0")
SPATIALITE_EXT = "/opt/homebrew/lib/mod_spatialite"
PG_DSN = os.environ.get("PG_DSN", "dbname=sqlite_tg_knn_bench")
EARTH_R = 6371008.8
# 256 MB page cache for every SQLite-based system; PostgreSQL and DuckDB
# bring their own buffer pools
SQLITE_CACHE = "pragma cache_size = -262144"
KS = [1, 10, 100, 1000]
RADIUS_M = 2000.0
N_QUERIES = 100


# ---------------------------------------------------------------------------
# data
# ---------------------------------------------------------------------------


def load_points(dataset):
    """-> (ids int64[N], names list, x float64[N], y float64[N])"""
    import duckdb

    path = DATA / f"places-{dataset}.parquet"
    # ids are assigned by Overture's GERS id order, so every system (and every
    # process) agrees on them; a bare row_number() over a parallel parquet
    # scan would not be deterministic
    rel = duckdb.sql(
        f"select row_number() over (order by id) as id, coalesce(name, '') as name, x, y "
        f"from '{path}'"
    )
    arrs = rel.fetchnumpy()
    return arrs["id"].astype(np.int64), arrs["name"].tolist(), arrs["x"].astype(np.float64), arrs["y"].astype(np.float64)


def query_points(x, y, n=N_QUERIES, seed=1):
    """80% jittered copies of real places (dense areas), 20% uniform over the
    bbox (deserts, ocean margins: the awkward case for expanding-box hacks)."""
    rng = np.random.default_rng(seed)
    idx = rng.integers(0, len(x), size=int(n * 0.8))
    qx = np.concatenate([x[idx] + rng.normal(0, 0.01, len(idx)), rng.uniform(x.min(), x.max(), n - len(idx))])
    qy = np.concatenate([y[idx] + rng.normal(0, 0.01, len(idx)), rng.uniform(y.min(), y.max(), n - len(idx))])
    return list(zip(qx.tolist(), qy.tolist()))


def haversine_m(qx, qy, x, y):
    lat1 = math.radians(qy)
    lat2 = np.radians(y)
    dlat = lat2 - lat1
    dlon = np.radians(x - qx)
    h = np.sin(dlat / 2) ** 2 + math.cos(lat1) * np.cos(lat2) * np.sin(dlon / 2) ** 2
    return 2 * EARTH_R * np.arcsin(np.sqrt(np.clip(h, 0, 1)))


def wkb_point(x, y):
    return struct.pack("<BIdd", 1, 1, x, y)


# ---------------------------------------------------------------------------
# systems. Each has: name, build(ids, names, x, y) -> None, open(), knn(qx, qy, k)
# -> list of (id, meters), and optionally radius(qx, qy, meters) -> list of ids.
# ---------------------------------------------------------------------------


class System:
    name = "?"
    persistent = True  # on-disk: build once, `run` reopens

    def path(self, dataset):
        return DATA / f"{dataset}-{self.name}.db"

    def size(self, dataset):
        p = self.path(dataset)
        return p.stat().st_size if p.exists() else None

    def build(self, dataset, ids, names, x, y):
        raise NotImplementedError

    def open(self, dataset):
        pass

    def knn(self, qx, qy, k):
        raise NotImplementedError

    def radius(self, qx, qy, meters):
        return None


class Tg0(System):
    name = "tg0"

    def build(self, dataset, ids, names, x, y):
        p = self.path(dataset)
        p.unlink(missing_ok=True)
        db = sqlite3.connect(p)
        db.enable_load_extension(True)
        db.load_extension(TG0_EXT)
        db.execute("pragma journal_mode = off")
        db.execute("pragma synchronous = off")
        db.execute("create virtual table places using tg0(name text, geom point)")
        db.executemany(
            "insert into places(rowid, name, geom) values (?, ?, ?)",
            ((int(i), n, wkb_point(px, py)) for i, n, px, py in zip(ids, names, x, y)),
        )
        db.commit()
        db.close()

    def open(self, dataset):
        self.db = sqlite3.connect(self.path(dataset))
        self.db.enable_load_extension(True)
        self.db.load_extension(TG0_EXT)
        self.db.execute(SQLITE_CACHE)
        self.knn_stmt = "select rowid, distance from places where geom match ? order by distance limit ?"

    def knn(self, qx, qy, k):
        return self.db.execute(self.knn_stmt, (wkb_point(qx, qy), k)).fetchall()

    def radius(self, qx, qy, meters):
        return [r[0] for r in self.db.execute(
            "select rowid from places where geom match ? and distance <= ? order by distance",
            (wkb_point(qx, qy), meters),
        )]


class SpatiaLite(System):
    """SpatiaLite 5's KNN2 virtual table over its R*Tree spatial index. KNN2
    needs a search radius in CRS units (degrees) and ranks by planar CRS
    distance (distance_crs), reporting geodesic meters alongside."""

    name = "spatialite"
    # KNN2 is a radius search that sorts what it finds: a big radius scans
    # everything inside it (0.5 degrees around Los Angeles takes ~5 s), so
    # start at ~100 m and let expand=1 double it until k rows are found
    knn_radius_deg = 0.001

    def _connect(self, p):
        db = sqlite3.connect(p)
        db.enable_load_extension(True)
        db.load_extension(SPATIALITE_EXT)
        db.execute("pragma trusted_schema = 1")
        return db

    def build(self, dataset, ids, names, x, y):
        p = self.path(dataset)
        p.unlink(missing_ok=True)
        db = self._connect(p)
        db.execute("pragma journal_mode = off")
        db.execute("pragma synchronous = off")
        db.execute("select InitSpatialMetaData(1)")
        db.execute("create table places(fid integer primary key, name text)")
        db.execute("select AddGeometryColumn('places', 'geom', 4326, 'POINT', 'XY')")
        db.executemany(
            "insert into places(fid, name, geom) values (?, ?, MakePoint(?, ?, 4326))",
            ((int(i), n, float(px), float(py)) for i, n, px, py in zip(ids, names, x, y)),
        )
        db.execute("select CreateSpatialIndex('places', 'geom')")
        db.commit()
        db.close()

    def open(self, dataset):
        self.db = self._connect(self.path(dataset))
        self.db.execute(SQLITE_CACHE)

    def knn(self, qx, qy, k):
        return self.db.execute(
            "select fid, distance_m from KNN2 where f_table_name = 'places' and f_geometry_column = 'geom' "
            # constraint order matters to KNN2: with max_items written before
            # radius it silently returns no rows (SpatiaLite 5.1.0)
            "and ref_geometry = MakePoint(?, ?, 4326) and radius = ? and max_items = ? and expand = 1",
            (qx, qy, self.knn_radius_deg, k),
        ).fetchall()


class SqliteRtreeExpand(System):
    """What you would hand-roll without tg0: a plain SQLite R*Tree of the
    points plus an expanding-box search in the application. Starts with a
    small box, quadruples it until k rows are inside, then re-queries with a
    box that provably covers the k-th geodesic distance and ranks the
    candidates by haversine in numpy."""

    name = "sqlite-rtree"

    def build(self, dataset, ids, names, x, y):
        p = self.path(dataset)
        p.unlink(missing_ok=True)
        db = sqlite3.connect(p)
        db.execute("pragma journal_mode = off")
        db.execute("pragma synchronous = off")
        db.execute("create table places(id integer primary key, name text, x real, y real)")
        db.execute("create virtual table places_idx using rtree(id, minx, maxx, miny, maxy)")
        db.executemany("insert into places values (?, ?, ?, ?)", zip(ids.tolist(), names, x.tolist(), y.tolist()))
        db.executemany("insert into places_idx values (?, ?, ?, ?, ?)", zip(ids.tolist(), x.tolist(), x.tolist(), y.tolist(), y.tolist()))
        db.commit()
        db.close()

    def open(self, dataset):
        self.db = sqlite3.connect(self.path(dataset))
        self.db.execute(SQLITE_CACHE)

    def _box(self, qx, qy, half_lon, half_lat):
        rows = self.db.execute(
            "select p.id, p.x, p.y from places_idx i join places p on p.id = i.id "
            "where i.minx <= ? and i.maxx >= ? and i.miny <= ? and i.maxy >= ?",
            (qx + half_lon, qx - half_lon, qy + half_lat, qy - half_lat),
        ).fetchall()
        return rows

    def _rank(self, rows, qx, qy, k):
        if not rows:
            return []
        arr = np.array(rows, dtype=np.float64)
        d = haversine_m(qx, qy, arr[:, 1], arr[:, 2])
        order = np.argsort(d, kind="stable")[:k]
        return [(int(arr[i, 0]), float(d[i])) for i in order]

    def knn(self, qx, qy, k):
        half = 0.002  # degrees; ~200 m
        while True:
            rows = self._box(qx, qy, half, half)
            if len(rows) >= k or half > 360:
                break
            half *= 4
        ranked = self._rank(rows, qx, qy, k)
        if not ranked:
            return []
        # the k-th candidate's geodesic distance bounds the answer; re-query
        # with a box that covers that distance in every direction
        dk = ranked[-1][1]
        half_lat = dk / 111_000 * 1.01 + 1e-9
        half_lon = half_lat / max(math.cos(math.radians(qy)), 1e-6)
        if half_lat > half or half_lon > half:
            rows = self._box(qx, qy, half_lon, half_lat)
            ranked = self._rank(rows, qx, qy, k)
        return ranked

    def radius(self, qx, qy, meters):
        half_lat = meters / 111_000 * 1.01
        half_lon = half_lat / max(math.cos(math.radians(qy)), 1e-6)
        rows = self._box(qx, qy, half_lon, half_lat)
        ranked = self._rank(rows, qx, qy, len(rows))
        return [i for i, d in ranked if d <= meters]


class PostGIS(System):
    """PostGIS 3.5 geography KNN: ORDER BY geom <-> point LIMIT k on a GiST
    index (exact spheroidal distances)."""

    name = "postgis"

    def size(self, dataset):
        import psycopg

        with psycopg.connect(PG_DSN) as c:
            return c.execute(f"select pg_total_relation_size('places_{dataset}')").fetchone()[0]

    def build(self, dataset, ids, names, x, y):
        import psycopg

        t = f"places_{dataset}"
        with psycopg.connect(PG_DSN) as c:
            c.execute(f"drop table if exists {t}")
            c.execute(f"create table {t}(id int primary key, name text, geom geography(Point, 4326))")
            with c.cursor().copy(f"copy {t}(id, name, geom) from stdin") as copy:
                for i, n, px, py in zip(ids.tolist(), names, x.tolist(), y.tolist()):
                    copy.write_row((i, n, f"SRID=4326;POINT({px} {py})"))
            c.execute(f"create index on {t} using gist(geom)")
            c.execute(f"analyze {t}")
            c.commit()

    def open(self, dataset):
        import psycopg

        self.c = psycopg.connect(PG_DSN, autocommit=True)
        self.t = f"places_{dataset}"
        self.c.execute("set jit = off")

    def knn(self, qx, qy, k):
        q = "ST_SetSRID(ST_MakePoint(%s, %s), 4326)::geography"
        return self.c.execute(
            f"select id, ST_Distance(geom, {q}) from {self.t} order by geom <-> {q} limit %s",
            (qx, qy, qx, qy, k),
        ).fetchall()

    def radius(self, qx, qy, meters):
        q = "ST_SetSRID(ST_MakePoint(%s, %s), 4326)::geography"
        return [r[0] for r in self.c.execute(
            f"select id from {self.t} where ST_DWithin(geom, {q}, %s) order by geom <-> {q}",
            (qx, qy, meters, qx, qy),
        )]


class DuckDB(System):
    """DuckDB spatial, no index: ORDER BY ST_Distance_Sphere LIMIT k — a
    vectorized brute-force top-k over every row."""

    name = "duckdb"

    def path(self, dataset):
        return DATA / f"{dataset}-duckdb.duckdb"

    def build(self, dataset, ids, names, x, y):
        import duckdb

        p = self.path(dataset)
        p.unlink(missing_ok=True)
        c = duckdb.connect(str(p))
        c.execute("install spatial; load spatial")
        c.execute(
            f"create table places as select row_number() over (order by id) as id, coalesce(name, '') as name, "
            f"ST_Point(x, y) as geom from '{DATA / f'places-{dataset}.parquet'}'"
        )
        c.close()

    def open(self, dataset):
        import duckdb

        self.c = duckdb.connect(str(self.path(dataset)), read_only=True)
        self.c.execute("load spatial")
        # ST_Distance_Sphere defaults to [lat, lon] axis order in 1.5
        self.c.execute("set geometry_always_xy = true")

    def knn(self, qx, qy, k):
        return self.c.execute(
            "select id, ST_Distance_Sphere(geom, ST_Point(?, ?)) as d from places order by d limit ?",
            (qx, qy, k),
        ).fetchall()

    def radius(self, qx, qy, meters):
        return [r[0] for r in self.c.execute(
            "select id from places where ST_Distance_Sphere(geom, ST_Point(?, ?)) <= ? order by ST_Distance_Sphere(geom, ST_Point(?, ?))",
            (qx, qy, meters, qx, qy),
        ).fetchall()]


class DuckDBRtreeExpand(DuckDB):
    """DuckDB spatial with an R-tree index (usable for ST_Within against a
    constant box, not for ordering), driven by the same expanding-box search
    as the plain SQLite R-tree variant."""

    name = "duckdb-rtree"

    def path(self, dataset):
        return DATA / f"{dataset}-duckdb-rtree.duckdb"

    def build(self, dataset, ids, names, x, y):
        import duckdb

        super().build(dataset, ids, names, x, y)
        c = duckdb.connect(str(self.path(dataset)))
        c.execute("load spatial")
        c.execute("create index places_rtree on places using rtree(geom)")
        c.close()

    def _box(self, qx, qy, half_lon, half_lat):
        return self.c.execute(
            "select id, ST_X(geom), ST_Y(geom) from places where ST_Within(geom, ST_MakeEnvelope(?, ?, ?, ?))",
            (qx - half_lon, qy - half_lat, qx + half_lon, qy + half_lat),
        ).fetchall()

    _rank = SqliteRtreeExpand._rank
    knn = SqliteRtreeExpand.knn
    radius = SqliteRtreeExpand.radius


def to_xyz(x, y):
    lat = np.radians(y)
    lon = np.radians(x)
    return np.column_stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)])


class KDTreeXYZ(System):
    """In-memory scipy cKDTree on unit-sphere vectors: chord distance is
    monotone in great-circle distance, so this is exact geodesic KNN for
    points (rebuilt from the parquet on every open; no on-disk index)."""

    name = "kdtree-xyz"
    persistent = False

    def build(self, dataset, ids, names, x, y):
        self.open(dataset, (ids, x, y))

    def open(self, dataset, data=None):
        from scipy.spatial import cKDTree

        ids, _, x, y = load_points(dataset) if data is None else (data[0], None, data[1], data[2])
        self.ids = ids
        self.tree = cKDTree(to_xyz(x, y))

    def knn(self, qx, qy, k):
        d, i = self.tree.query(to_xyz(np.array([qx]), np.array([qy]))[0], k=k)
        d = np.atleast_1d(d)
        i = np.atleast_1d(i)
        meters = 2 * EARTH_R * np.arcsin(np.clip(d / 2, 0, 1))
        return [(int(self.ids[j]), float(m)) for j, m in zip(i, meters)]

    def radius(self, qx, qy, meters):
        chord = 2 * math.sin(meters / EARTH_R / 2)
        idx = self.tree.query_ball_point(to_xyz(np.array([qx]), np.array([qy]))[0], chord)
        return [int(self.ids[j]) for j in idx]


class KDTreeDegrees(System):
    """In-memory scipy cKDTree on raw lon/lat degrees — the planar-on-degrees
    mistake: fast, but ranks by Euclidean degrees, which is wrong away from
    the equator. Included to measure how often that changes the answer."""

    name = "kdtree-degrees"
    persistent = False

    def build(self, dataset, ids, names, x, y):
        self.open(dataset, (ids, x, y))

    def open(self, dataset, data=None):
        from scipy.spatial import cKDTree

        ids, _, x, y = load_points(dataset) if data is None else (data[0], None, data[1], data[2])
        self.ids, self.x, self.y = ids, x, y
        self.tree = cKDTree(np.column_stack([x, y]))

    def knn(self, qx, qy, k):
        d, i = self.tree.query([qx, qy], k=k)
        i = np.atleast_1d(i)
        return [(int(self.ids[j]), float(haversine_m(qx, qy, self.x[j], self.y[j]))) for j in i]


class BallTreeHaversine(System):
    """In-memory scikit-learn BallTree with the haversine metric."""

    name = "balltree-haversine"
    persistent = False

    def build(self, dataset, ids, names, x, y):
        self.open(dataset, (ids, x, y))

    def open(self, dataset, data=None):
        from sklearn.neighbors import BallTree

        ids, _, x, y = load_points(dataset) if data is None else (data[0], None, data[1], data[2])
        self.ids = ids
        self.tree = BallTree(np.column_stack([np.radians(y), np.radians(x)]), metric="haversine")

    def knn(self, qx, qy, k):
        d, i = self.tree.query([[math.radians(qy), math.radians(qx)]], k=k)
        return [(int(self.ids[j]), float(m * EARTH_R)) for j, m in zip(i[0], d[0])]


class BruteNumpy(System):
    """Reference: haversine to every row in numpy, argpartition top-k."""

    name = "brute-numpy"
    persistent = False

    def build(self, dataset, ids, names, x, y):
        self.open(dataset, (ids, x, y))

    def open(self, dataset, data=None):
        ids, _, x, y = load_points(dataset) if data is None else (data[0], None, data[1], data[2])
        self.ids, self.x, self.y = ids, x, y

    def knn(self, qx, qy, k):
        d = haversine_m(qx, qy, self.x, self.y)
        k = min(k, len(d))
        part = np.argpartition(d, k - 1)[:k]
        order = part[np.lexsort((self.ids[part], d[part]))]
        return [(int(self.ids[j]), float(d[j])) for j in order]

    def radius(self, qx, qy, meters):
        d = haversine_m(qx, qy, self.x, self.y)
        return [int(i) for i in self.ids[d <= meters]]


SYSTEMS = {
    s.name: s
    for s in [Tg0(), SpatiaLite(), SqliteRtreeExpand(), PostGIS(), DuckDB(), DuckDBRtreeExpand(), KDTreeXYZ(), KDTreeDegrees(), BallTreeHaversine(), BruteNumpy()]
}


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------


def results_path(dataset):
    return HERE / f"results-{dataset}.json"


def load_results(dataset):
    p = results_path(dataset)
    return json.loads(p.read_text()) if p.exists() else {"systems": {}}


def save_results(dataset, results):
    results_path(dataset).write_text(json.dumps(results, indent=1, sort_keys=True))


def cmd_build(args):
    dataset = args.dataset
    ids, names, x, y = load_points(dataset)
    results = load_results(dataset)
    results["rows"] = int(len(ids))
    for name in args.systems:
        s = SYSTEMS[name]
        print(f"[{name}] building {len(ids):,} rows ...", flush=True)
        t0 = time.perf_counter()
        s.build(dataset, ids, names, x, y)
        dt = time.perf_counter() - t0
        size = s.size(dataset) if s.persistent else None
        print(f"[{name}] built in {dt:.1f}s, size {size / 1e6 if size else 0:.1f} MB", flush=True)
        results["systems"].setdefault(name, {})["build_s"] = dt
        results["systems"][name]["size_bytes"] = size
        results["systems"][name]["doc"] = (s.__doc__ or "").strip()
        save_results(dataset, results)


def cmd_run(args):
    dataset = args.dataset
    ids, _, x, y = load_points(dataset)
    assert (ids == np.arange(1, len(ids) + 1)).all()
    queries = query_points(x, y)
    results = load_results(dataset)
    ref = BruteNumpy()
    ref.open(dataset, (ids, x, y))
    reference = {k: [ref.knn(qx, qy, k) for qx, qy in queries] for k in KS}
    reference_radius = [set(ref.radius(qx, qy, RADIUS_M)) for qx, qy in queries]

    def true_dist(qx, qy, got_ids):
        # haversine recomputed here for the ids a system returned, so ties at
        # the k-th distance (Overture has many co-located places) and
        # sphere-vs-spheroid reporting differences do not count as errors
        j = np.array(got_ids, dtype=np.int64) - 1
        return haversine_m(qx, qy, x[j], y[j])
    for name in args.systems:
        s = SYSTEMS[name]
        print(f"[{name}] opening ...", flush=True)
        t0 = time.perf_counter()
        s.open(dataset)
        open_s = time.perf_counter() - t0
        entry = results["systems"].setdefault(name, {})
        entry["open_s"] = open_s
        entry["knn"] = {}
        # warm-up pass over every query point at the largest k so first-touch
        # page faults are not charged to whichever k runs first
        for qx, qy in queries:
            s.knn(qx, qy, max(KS))
        for k in KS:
            times = []
            exact_set = 0
            exact_order = 0
            dist_err = 0.0
            for (qx, qy), want in zip(queries, reference[k]):
                t0 = time.perf_counter()
                got = s.knn(qx, qy, k)
                times.append(time.perf_counter() - t0)
                got_ids = [g[0] for g in got]
                want_d = np.array([w[1] for w in want])
                if len(got_ids) == len(want) and len(got_ids) > 0:
                    got_d = true_dist(qx, qy, got_ids)
                    tol = np.maximum(want_d * 1e-9, 1e-6)
                    if np.all(np.abs(np.sort(got_d) - want_d) <= tol):
                        exact_set += 1
                        if np.all(np.diff(got_d) >= -tol[1:]):
                            exact_order += 1
                    # how far the system's own reported k-th distance is from
                    # the haversine (sphere vs spheroid, planar degrees, ...)
                    if want_d[-1] > 0 and got[-1][1] is not None:
                        dist_err = max(dist_err, abs(float(got[-1][1]) - float(got_d[-1])) / float(got_d[-1]))
            times = np.array(times) * 1000
            entry["knn"][str(k)] = {
                "median_ms": float(np.median(times)),
                "p95_ms": float(np.percentile(times, 95)),
                "mean_ms": float(times.mean()),
                "max_ms": float(times.max()),
                "set_match": exact_set / len(queries),
                "order_match": exact_order / len(queries),
                "kth_dist_rel_err": dist_err,
            }
            print(f"[{name}] k={k:<5} median {np.median(times):8.3f} ms  p95 {np.percentile(times, 95):8.3f} ms  set-match {exact_set / len(queries):.0%}", flush=True)
        if s.radius(*queries[0], RADIUS_M) is not None:
            times = []
            match = 0
            for (qx, qy), want in zip(queries, reference_radius):
                t0 = time.perf_counter()
                got = s.radius(qx, qy, RADIUS_M)
                times.append(time.perf_counter() - t0)
                # rows within a hair of the radius may legitimately differ
                # between sphere and spheroid; compare after dropping those
                # (spheroidal distances differ from the sphere by up to ~0.5%)
                got_d = true_dist(qx, qy, got) if got else np.array([])
                inner_got = {i for i, d in zip(got, got_d) if d <= RADIUS_M * (1 - 1e-2)}
                inner_want = {i for i in want if haversine_m(qx, qy, x[i - 1], y[i - 1]) <= RADIUS_M * (1 - 1e-2)}
                outer_ok = all(d <= RADIUS_M * (1 + 1e-2) for d in got_d)
                match += inner_got == inner_want and outer_ok
            times = np.array(times) * 1000
            entry["radius"] = {
                "meters": RADIUS_M,
                "median_ms": float(np.median(times)),
                "p95_ms": float(np.percentile(times, 95)),
                "set_match": match / len(queries),
                "avg_rows": float(np.mean([len(w) for w in reference_radius])),
            }
            print(f"[{name}] radius {RADIUS_M:.0f} m: median {np.median(times):8.3f} ms  set-match {match / len(queries):.0%}", flush=True)
        save_results(dataset, results)


def cmd_report(args):
    dataset = args.dataset
    results = load_results(dataset)
    rows = results.get("rows", 0)
    lines = [f"### {dataset}: {rows:,} Overture places\n"]
    lines.append("| system | build | size | k=1 | k=10 | k=100 | k=1000 | radius 2 km | correct (k=10) |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    order = [n for n in SYSTEMS if n in results["systems"]]
    for name in order:
        e = results["systems"][name]
        def cell(k):
            r = e.get("knn", {}).get(str(k))
            return f"{r['median_ms']:.2f} ms" if r else "—"
        size = e.get("size_bytes")
        rad = e.get("radius")
        k10 = e.get("knn", {}).get("10")
        lines.append(
            f"| {name} | {e.get('build_s', 0):.0f} s | {size / 1e6:.0f} MB | " if size else f"| {name} | {e.get('build_s', 0):.0f} s | in-memory | "
        )
        lines[-1] += f"{cell(1)} | {cell(10)} | {cell(100)} | {cell(1000)} | "
        lines[-1] += (f"{rad['median_ms']:.2f} ms" if rad else "—") + " | "
        lines[-1] += (f"{k10['set_match']:.0%} sets, {k10['order_match']:.0%} order" if k10 else "—") + " |"
    lines.append("\nTail latency (p95), same runs:\n")
    lines.append("| system | k=1 p95 | k=10 p95 | k=100 p95 | k=1000 p95 | radius p95 |")
    lines.append("|---|---|---|---|---|---|")
    for name in order:
        e = results["systems"][name]
        def p95(k):
            r = e.get("knn", {}).get(str(k))
            return f"{r['p95_ms']:.2f} ms" if r else "—"
        rad = e.get("radius")
        lines.append(f"| {name} | {p95(1)} | {p95(10)} | {p95(100)} | {p95(1000)} | " + (f"{rad['p95_ms']:.2f} ms" if rad else "—") + " |")
    lines.append("\nMedian latency per query over 100 query points (80 near real places, 20 uniform over the bbox), warm, Python driver overhead included for every system. Correctness = share of queries whose top-10 id set / order equals the numpy brute-force haversine reference.\n")
    out = "\n".join(lines)
    print(out)
    (HERE / f"results-{dataset}.md").write_text(out + "\n")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("build", "run", "report"):
        p = sub.add_parser(c)
        p.add_argument("--dataset", default="ca")
        p.add_argument("--systems", nargs="*", default=list(SYSTEMS))
    args = ap.parse_args()
    {"build": cmd_build, "run": cmd_run, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    main()

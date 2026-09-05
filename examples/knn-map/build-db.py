#!/usr/bin/env python3
"""Builds data/places-sf.db — a tg0 table of Overture places inside a San
Francisco bounding box — from the parquet the benchmark downloads
(bench/knn/download-places.sql). Run from the repo root after `make loadable`:

    uv run --with duckdb python examples/knn-map/build-db.py
"""
import sqlite3
import struct
import sys
from pathlib import Path

import duckdb

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PARQUET = ROOT / "bench/knn/data.noindex/places-ca.parquet"
OUT = HERE / "data/places-sf.db"
BBOX = (-122.53, 37.70, -122.35, 37.84)

rows = duckdb.sql(
    f"select id, coalesce(name, '') as name, coalesce(category, '') as category, x, y "
    f"from '{PARQUET}' where x between {BBOX[0]} and {BBOX[2]} and y between {BBOX[1]} and {BBOX[3]} "
    f"order by id"
).fetchall()
OUT.unlink(missing_ok=True)
db = sqlite3.connect(OUT)
db.enable_load_extension(True)
db.load_extension(str(ROOT / "dist/tg0"))
db.execute("pragma page_size = 4096")
db.execute("create virtual table places using tg0(gers_id text, name text, category text, geom point)")
db.executemany(
    "insert into places(gers_id, name, category, geom) values (?, ?, ?, ?)",
    ((i, n, c, struct.pack("<BIdd", 1, 1, x, y)) for i, n, c, x, y in rows),
)
db.commit()
db.execute("vacuum")
db.close()
print(f"{len(rows):,} places -> {OUT} ({OUT.stat().st_size / 1e6:.1f} MB)")

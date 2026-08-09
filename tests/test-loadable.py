# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "pytest",
#     "shapely",
#     "syrupy",
# ]
# ///


import re
import sqlite3
import json
import unittest
from pathlib import Path
import inspect

import pytest
import shapely

EXT_PATH = "./dist/tg0"


def connect(ext, path=":memory:"):
    db = sqlite3.connect(path)

    db.execute(
        "create temp table base_functions as select name from pragma_function_list"
    )
    db.execute("create temp table base_modules as select name from pragma_module_list")

    db.enable_load_extension(True)
    db.load_extension(ext)

    db.execute(
        "create temp table loaded_functions as select name from pragma_function_list where name not in (select name from base_functions) order by name"
    )
    db.execute(
        "create temp table loaded_modules as select name from pragma_module_list where name not in (select name from base_modules) order by name"
    )

    db.row_factory = sqlite3.Row
    return db


db = connect(EXT_PATH)


def explain_query_plan(sql):
    return db.execute("explain query plan " + sql).fetchone()["detail"]


def execute_all(cursor, sql, args=None):
    if args is None:
        args = []
    results = cursor.execute(sql, args).fetchall()
    return list(map(lambda x: dict(x), results))


def spread_args(args):
    return ",".join(["?"] * len(args))


FUNCTIONS = [
    "tg_contains",
    "tg_coveredby",
    "tg_covers",
    "tg_debug",
    "tg_dims",
    "tg_disjoint",
    "tg_equals",
    "tg_extra_json",
    "tg_geom",
    "tg_geom",
    "tg_geometrycollection",
    "tg_group_bbox",
    "tg_group_feature_collection_geojson",
    "tg_group_feature_collection_geojson",
    "tg_group_geometry_collection",
    "tg_group_geometrycollection",
    "tg_group_line",
    "tg_group_multilinestring",
    "tg_group_multipoint",
    "tg_group_multipolygon",
    "tg_group_polygon",
    "tg_group_ring",
    "tg_has_m",
    "tg_has_z",
    "tg_intersects",
    "tg_is_empty",
    "tg_is_feature",
    "tg_is_featurecollection",
    "tg_line",
    "tg_linestring",
    "tg_m",
    "tg_max_x",
    "tg_max_y",
    "tg_memsize",
    "tg_min_x",
    "tg_min_y",
    "tg_multilinestring",
    "tg_multipoint",
    "tg_multipolygon",
    "tg_point",
    "tg_point",
    "tg_point",
    "tg_poly_exterior",
    "tg_polygon",
    "tg_rect",
    "tg_rect",
    "tg_ring",
    "tg_segment",
    "tg_segment",
    "tg_to_geojson",
    "tg_to_wkb",
    "tg_to_wkt",
    "tg_touches",
    "tg_type",
    "tg_valid_geojson",
    "tg_valid_wkb",
    "tg_valid_wkt",
    "tg_version",
    "tg_within",
    "tg_x",
    "tg_y",
    "tg_z",
]


MODULES = [
    "tg0",
    "tg_bbox",
    "tg_each",
    "tg_geometries_each",
    "tg_holes_each",
    "tg_line_points_each",
    "tg_line_segments_each",
    "tg_lines_each",
    "tg_points_each",
    "tg_polygons_each",
    "tg_ring_points_each",
    "tg_ring_segments_each",
]

SUPPORTS_SUBTYPE = sqlite3.version_info[1] > 38
SUPPORTS_RTREE = (
    sqlite3.connect(":memory:")
    .execute("select name from pragma_module_list where name = 'rtree';")
    .fetchone()
    is not None
)

LINE_A = shapely.from_wkt("LINESTRING (0 0, 1 1)")
LINE_B = shapely.from_wkt("LINESTRING (1 1, 2 2)")

LINE_CROSSES = (
    shapely.from_wkt("LINESTRING (0 0, 2 2)"),
    shapely.from_wkt("LINESTRING (1 0, 1 2)"),
)
LINE_SEPARATE = (
    shapely.from_wkt("LINESTRING (0 0, 0 2)"),
    shapely.from_wkt("LINESTRING (2 0, 2 2)"),
)

COLLECTION_GEOJSON = (Path(__file__).parent / "data" / "collection.geojson").read_text()


def test_funcs():
    funcs = list(
        map(
            lambda a: a[0],
            db.execute("select name from loaded_functions").fetchall(),
        )
    )
    assert funcs == FUNCTIONS


def test_modules():
    modules = list(
        map(lambda a: a[0], db.execute("select name from loaded_modules").fetchall())
    )
    assert modules == MODULES


def test_tg_version():
    assert db.execute("select tg_version()").fetchone()[0][0] == "v"


def test_tg_debug():
    debug = db.execute("select tg_debug()").fetchone()[0].split("\n")
    assert len(debug) == 5


def test_tg_intersects():
    tg_intersects = lambda *args: db.execute(
        "select tg_intersects(?, ?)", args
    ).fetchone()[0]

    for format in ("wkt", "wkb", "geojson"):
        assert (
            tg_intersects(*[getattr(shapely, f"to_{format}")(l) for l in LINE_CROSSES])
            == 1
        )

        assert (
            tg_intersects(*[getattr(shapely, f"to_{format}")(l) for l in LINE_SEPARATE])
            == 0
        )


def test_tg_equals():
    tg_equals = lambda *args: db.execute("select tg_equals(?, ?)", args).fetchone()[0]

    POINT_A = shapely.from_wkt("POINT (1 1)")
    POINT_A_AGAIN = shapely.from_wkt("POINT (1 1)")
    POINT_B = shapely.from_wkt("POINT (2 2)")

    for format in ("wkt", "wkb", "geojson"):
        to_format = getattr(shapely, f"to_{format}")
        assert tg_equals(to_format(POINT_A), to_format(POINT_A_AGAIN)) == 1
        assert tg_equals(to_format(POINT_A), to_format(POINT_B)) == 0

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_equals(None, "POINT(1 1)")


def test_tg_type():
    tg_type = lambda *args: db.execute("select tg_type(?)", args).fetchone()[0]
    assert tg_type(shapely.to_wkt(LINE_A)) == "LineString"

    assert tg_type("POINT (30 10)") == "Point"
    assert tg_type("LINESTRING (30 10, 10 30, 40 40)") == "LineString"
    assert tg_type("POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))") == "Polygon"
    assert tg_type("MULTIPOINT (10 40, 40 30, 20 20, 30 10)") == "MultiPoint"
    assert (
        tg_type(
            "MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))"
        )
        == "MultiPolygon"
    )
    assert (
        tg_type(
            "GEOMETRYCOLLECTION (POINT (40 10),LINESTRING (10 10, 20 20, 10 40),POLYGON ((40 40, 20 45, 45 30, 40 40)))"
        )
        == "GeometryCollection"
    )

    db.execute("select tg_type(tg_point(1, 2)) from json_each('[1,2,3]')").fetchall()


def test_tg_dims():
    tg_dims = lambda *args: db.execute("select tg_dims(?)", args).fetchone()[0]
    for format in ("wkt", "wkb", "geojson"):
        assert tg_dims(getattr(shapely, f"to_{format}")(LINE_A)) == 2
    assert db.execute("select tg_dims(tg_point(1, 2))").fetchone()[0] == 2

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_dims(None)


def test_tg_is_empty():
    tg_is_empty = lambda *args: db.execute("select tg_is_empty(?)", args).fetchone()[0]
    assert tg_is_empty("POINT EMPTY") == 1
    assert tg_is_empty("POINT (1 1)") == 0
    assert tg_is_empty(shapely.to_wkb(LINE_A)) == 0
    assert tg_is_empty(shapely.to_geojson(LINE_A)) == 0
    assert db.execute("select tg_is_empty(tg_point(1, 2))").fetchone()[0] == 0

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_is_empty(None)


def test_tg_is_feature():
    tg_is_feature = lambda *args: db.execute("select tg_is_feature(?)", args).fetchone()[
        0
    ]
    assert (
        tg_is_feature(
            '{"type":"Feature","geometry":{"type":"Point","coordinates":[1,2]},"properties":{}}'
        )
        == 1
    )
    assert tg_is_feature('{"type":"Point","coordinates":[1,2]}') == 0
    assert tg_is_feature('{"type":"FeatureCollection","features":[]}') == 0
    assert tg_is_feature("POINT (1 2)") == 0

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_is_feature(None)


def test_tg_is_featurecollection():
    tg_is_featurecollection = lambda *args: db.execute(
        "select tg_is_featurecollection(?)", args
    ).fetchone()[0]
    assert (
        tg_is_featurecollection('{"type":"FeatureCollection","features":[]}') == 1
    )
    assert tg_is_featurecollection('{"type":"Point","coordinates":[1,2]}') == 0
    assert (
        tg_is_featurecollection(
            '{"type":"Feature","geometry":{"type":"Point","coordinates":[1,2]},"properties":{}}'
        )
        == 0
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_is_featurecollection(None)


def test_tg_memsize():
    tg_memsize = lambda *args: db.execute("select tg_memsize(?)", args).fetchone()[0]
    for format in ("wkt", "wkb", "geojson"):
        assert tg_memsize(getattr(shapely, f"to_{format}")(LINE_A)) > 0
    assert db.execute("select tg_memsize(tg_point(1, 2))").fetchone()[0] > 0

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_memsize(None)


def test_tg_x():
    tg_x = lambda *args: db.execute("select tg_x(?)", args).fetchone()[0]
    POINT = shapely.from_wkt("POINT (1 2)")
    for format in ("wkt", "wkb", "geojson"):
        assert tg_x(getattr(shapely, f"to_{format}")(POINT)) == 1
    assert db.execute("select tg_x(tg_point(1, 2))").fetchone()[0] == 1

    with pytest.raises(
        sqlite3.OperationalError,
        match="tg_x\\(\\) expected a Point geometry, found LineString",
    ):
        tg_x(shapely.to_wkt(LINE_A))

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_x(None)


def test_tg_y():
    tg_y = lambda *args: db.execute("select tg_y(?)", args).fetchone()[0]
    POINT = shapely.from_wkt("POINT (1 2)")
    for format in ("wkt", "wkb", "geojson"):
        assert tg_y(getattr(shapely, f"to_{format}")(POINT)) == 2
    assert db.execute("select tg_y(tg_point(1, 2))").fetchone()[0] == 2

    with pytest.raises(
        sqlite3.OperationalError,
        match="tg_y\\(\\) expected a Point geometry, found LineString",
    ):
        tg_y(shapely.to_wkt(LINE_A))

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_y(None)


def test_tg_min_x_min_y_max_x_max_y():
    POLY = shapely.from_wkt("POLYGON ((1 1, 2 2, 3 3, 1 1))")
    for format in ("wkt", "wkb", "geojson"):
        value = getattr(shapely, f"to_{format}")(POLY)
        assert db.execute("select tg_min_x(?)", [value]).fetchone()[0] == 1
        assert db.execute("select tg_min_y(?)", [value]).fetchone()[0] == 1
        assert db.execute("select tg_max_x(?)", [value]).fetchone()[0] == 3
        assert db.execute("select tg_max_y(?)", [value]).fetchone()[0] == 3

    row = db.execute(
        "select tg_min_x(tg_geom(?)), tg_min_y(tg_geom(?)), "
        "tg_max_x(tg_geom(?)), tg_max_y(tg_geom(?))",
        ["POLYGON ((1 1, 2 2, 3 3, 1 1))"] * 4,
    ).fetchone()
    assert tuple(row) == (1, 1, 3, 3)

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        db.execute("select tg_min_x(?)", [None])


def test_tg_to_wkt():
    tg_to_wkt = lambda *args: db.execute("select tg_to_wkt(?)", args).fetchone()[0]
    assert tg_to_wkt(shapely.to_wkb(LINE_A)) == "LINESTRING(0 0,1 1)"
    # TODO more tests


def test_tg_to_wkb(snapshot):
    tg_to_wkb = lambda *args: db.execute("select tg_to_wkb(?)", args).fetchone()[0]
    assert tg_to_wkb(shapely.to_wkt(LINE_A)) == snapshot
    # TODO more tests


def test_tg_extra_json():
    tg_extra_json = lambda *args: db.execute(
        "select tg_extra_json(?)", args
    ).fetchone()[0]
    assert (
        tg_extra_json('{"type": "Point","coordinates": [-118.2097812,34.0437074]}')
        == None
    )
    assert (
        tg_extra_json(
            '{"id": "A", "type": "Point","coordinates": [-118.2097812,34.0437074]}'
        )
        == '{"id":"A"}'
    )


def test_tg_geom():
    assert db.execute("select tg_geom(?)", ["POINT(1 1)"]).fetchone()[0] == None
    if SUPPORTS_SUBTYPE:
        assert (
            db.execute("select subtype(tg_geom(?))", ["POINT(1 1)"]).fetchone()[0]
            == 112
        )
    with pytest.raises(sqlite3.OperationalError, match="ParseError: invalid text"):
        db.execute("select tg_geom(?)", ["POINT(1 1"]).fetchone()


def test_tg_valid_geojson():
    tg_valid_geojson = lambda *args: db.execute(
        "select tg_valid_geojson(?)", args
    ).fetchone()[0]
    assert tg_valid_geojson(shapely.to_geojson(LINE_A)) == 1
    assert tg_valid_geojson(shapely.to_wkt(LINE_A)) == 0


def test_tg_valid_wkb():
    tg_valid_wkb = lambda *args: db.execute("select tg_valid_wkb(?)", args).fetchone()[
        0
    ]
    assert tg_valid_wkb(shapely.to_wkb(LINE_A)) == 1
    assert tg_valid_wkb(shapely.to_wkt(LINE_A)) == 0


def test_tg_valid_wkt():
    tg_valid_wkt = lambda *args: db.execute("select tg_valid_wkt(?)", args).fetchone()[
        0
    ]
    assert tg_valid_wkt(shapely.to_wkt(LINE_A)) == 1
    assert tg_valid_wkt(shapely.to_wkb(LINE_A)) == 0


def test_tg_geometries_each(snapshot):
    tg_geometries_each = lambda *args: execute_all(
        db, "select rowid, * from tg_geometries_each(?)", args
    )
    assert (
        execute_all(
            db,
            "select rowid, geometry, tg_to_geojson(geometry) from tg_geometries_each(?)",
            [COLLECTION_GEOJSON],
        )
        == snapshot
    )
    # with pytest.raises(sqlite3.OperationalError, match="XXX"):
    #    tg_geometries_each(1)


def test_tg_lines_each(snapshot):
    tg_lines_each = lambda *args: execute_all(
        db, "select rowid, tg_to_wkt(line) as line_wkt from tg_lines_each(?)", args
    )
    assert tg_lines_each("MULTILINESTRING ()") == []
    assert (
        tg_lines_each(
            "MULTILINESTRING ((10 10, 20 20, 10 40), (40 40, 30 30, 40 20, 30 10))"
        )
        == snapshot()
    )

    # TODO: should this throw, since the input isn't a MULTILINESTRING?
    # tg_lines_each('LINESTRING(10 10,20 20,10 40)')


def test_tg_line_points_each(snapshot):
    tg_line_points_each = lambda *args: execute_all(
        db,
        "select rowid, tg_to_wkt(point) as point_wkt from tg_line_points_each(?)",
        args,
    )
    assert tg_line_points_each("LINESTRING EMPTY") == []
    assert (
        tg_line_points_each("LINESTRING (10 10, 20 20, 10 40, 40 40)") == snapshot
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match="expected a LineString geometry, found Point",
    ):
        tg_line_points_each("POINT (1 1)")

    with pytest.raises(sqlite3.OperationalError, match="invalid geometry input"):
        tg_line_points_each(None)

    with pytest.raises(sqlite3.OperationalError):
        tg_line_points_each("not a geometry")


def test_tg_line_segments_each(snapshot):
    tg_line_segments_each = lambda *args: execute_all(
        db,
        "select rowid, ax, ay, bx, by from tg_line_segments_each(?)",
        args,
    )
    assert tg_line_segments_each("LINESTRING EMPTY") == []
    assert (
        tg_line_segments_each("LINESTRING (10 10, 20 20, 10 40, 40 40)") == snapshot
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match="expected a LineString geometry, found Polygon",
    ):
        tg_line_segments_each("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))")

    with pytest.raises(sqlite3.OperationalError, match="invalid geometry input"):
        tg_line_segments_each(None)


def test_tg_ring_points_each(snapshot):
    from_exterior = execute_all(
        db,
        """
        select rowid, tg_to_wkt(point) as point_wkt
        from tg_ring_points_each(
          tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))')
        )
        """,
    )
    assert from_exterior == snapshot

    # a plain Polygon is also accepted: its exterior ring is used automatically
    from_polygon = execute_all(
        db,
        "select rowid, tg_to_wkt(point) as point_wkt from tg_ring_points_each(?)",
        ["POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))"],
    )
    assert from_polygon == from_exterior

    # a hole ring (also an upcast ring pointer, same as tg_poly_exterior())
    # works the same way
    from_hole = execute_all(
        db,
        """
        select tg_ring_points_each.rowid, tg_to_wkt(point) as point_wkt
        from tg_holes_each(
          'POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))'
        )
        join tg_ring_points_each(hole)
        """,
    )
    assert len(from_hole) == 5

    with pytest.raises(
        sqlite3.OperationalError,
        match="expected a ring .* or a Polygon geometry, found Point",
    ):
        execute_all(db, "select * from tg_ring_points_each(?)", ["POINT (1 1)"])

    with pytest.raises(sqlite3.OperationalError, match="invalid geometry input"):
        execute_all(db, "select * from tg_ring_points_each(?)", [None])


def test_tg_ring_segments_each(snapshot):
    from_exterior = execute_all(
        db,
        """
        select rowid, ax, ay, bx, by
        from tg_ring_segments_each(
          tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))')
        )
        """,
    )
    assert from_exterior == snapshot

    from_polygon = execute_all(
        db,
        "select rowid, ax, ay, bx, by from tg_ring_segments_each(?)",
        ["POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))"],
    )
    assert from_polygon == from_exterior

    with pytest.raises(
        sqlite3.OperationalError,
        match="expected a ring .* or a Polygon geometry, found MultiPoint",
    ):
        execute_all(
            db, "select * from tg_ring_segments_each(?)", ["MULTIPOINT (1 1, 2 2)"]
        )


@pytest.mark.skip(reason="TODO")
def test_tg_points_each():
    tg_points_each = lambda *args: execute_all(
        db, "select rowid, * from tg_points_each(?)", args
    )
    assert tg_points_each() == []
    with pytest.raises(sqlite3.OperationalError):
        tg_points_each()


@pytest.mark.skip(reason="TODO")
def test_tg_polygons_each():
    tg_polygons_each = lambda *args: execute_all(
        db, "select rowid, * from tg_polygons_each(?)", args
    )
    assert tg_polygons_each() == []
    with pytest.raises(sqlite3.OperationalError):
        tg_polygons_each()


@pytest.mark.skip(reason="TODO")
def test_tg_bbox():
    tg_tg_bbox = lambda *args: execute_all(
        db, "select rowid, * from tg_tg_bbox(?)", args
    )
    assert tg_tg_bbox() == []
    with pytest.raises(sqlite3.OperationalError):
        tg_tg_bbox()


def test_tg_to_geojson():
    tg_to_geojson = lambda *args: db.execute(
        "select tg_to_geojson(?)", args
    ).fetchone()[0]
    assert (
        tg_to_geojson(shapely.to_wkt(LINE_A))
        == '{"type":"LineString","coordinates":[[0,0],[1,1]]}'
    )
    # TODO more tests


@pytest.mark.skip(reason="TODO")
def test_tg_point():
    pass

def test_tg_point():
    assert db.execute("select tg_point(1, 1)").fetchone()[0] == None
    assert db.execute("select tg_to_wkt(tg_point(1, 1))").fetchone()[0] == "POINT(1 1)"
    assert [
        row[0]
        for row in db.execute(
            "select tg_to_wkt(tg_point(1, 1)) from json_each('[1,2]')"
        ).fetchall()
    ] == ["POINT(1 1)", "POINT(1 1)"]

    if SUPPORTS_SUBTYPE:
        assert db.execute("select subtype(tg_point(1, 1))").fetchone()[0] == 4

    with pytest.raises(
        sqlite3.OperationalError, match="point X value must be an integer or float"
    ):
        assert db.execute("select tg_point('a', 1)")
    with pytest.raises(
        sqlite3.OperationalError, match="point Y value must be an integer or float"
    ):
        assert db.execute("select tg_point(1, 'a')")

    # Z/M arities: 3-arg = Z (WKT convention), 4-arg = ZM. M-only
    # construction is intentionally unsupported.
    assert (
        db.execute("select tg_to_wkt(tg_point(1, 2, 3))").fetchone()[0]
        == "POINT(1 2 3)"
    )
    assert (
        db.execute("select tg_to_wkt(tg_point(1, 2, 3, 4))").fetchone()[0]
        == "POINT(1 2 3 4)"
    )

    with pytest.raises(
        sqlite3.OperationalError, match="point Z value must be an integer or float"
    ):
        assert db.execute("select tg_point(1, 2, 'a')")
    with pytest.raises(
        sqlite3.OperationalError, match="point M value must be an integer or float"
    ):
        assert db.execute("select tg_point(1, 2, 3, 'a')")


def test_tg_has_z():
    tg_has_z = lambda *args: db.execute("select tg_has_z(?)", args).fetchone()[0]
    assert db.execute("select tg_has_z(tg_point(1, 2))").fetchone()[0] == 0
    assert db.execute("select tg_has_z(tg_point(1, 2, 3))").fetchone()[0] == 1
    assert db.execute("select tg_has_z(tg_point(1, 2, 3, 4))").fetchone()[0] == 1
    assert tg_has_z(shapely.to_wkt(LINE_A)) == 0
    assert tg_has_z("LINESTRING Z (0 0 1, 1 1 1)") == 1

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_has_z(None)


def test_tg_has_m():
    tg_has_m = lambda *args: db.execute("select tg_has_m(?)", args).fetchone()[0]
    assert db.execute("select tg_has_m(tg_point(1, 2))").fetchone()[0] == 0
    assert db.execute("select tg_has_m(tg_point(1, 2, 3))").fetchone()[0] == 0
    assert db.execute("select tg_has_m(tg_point(1, 2, 3, 4))").fetchone()[0] == 1
    assert tg_has_m(shapely.to_wkt(LINE_A)) == 0
    assert tg_has_m("LINESTRING ZM (0 0 1 1, 1 1 1 1)") == 1

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_has_m(None)


def test_tg_z():
    tg_z = lambda *args: db.execute("select tg_z(?)", args).fetchone()[0]
    # 2D point: no Z coordinate -> NULL, not 0 (0 would be ambiguous with a
    # real Z value of 0).
    assert tg_z("POINT (1 2)") is None
    assert db.execute("select tg_z(tg_point(1, 2, 3))").fetchone()[0] == 3
    assert db.execute("select tg_z(tg_point(1, 2, 3, 4))").fetchone()[0] == 3
    assert tg_z("POINT Z (1 2 3)") == 3

    with pytest.raises(
        sqlite3.OperationalError,
        match="tg_z\\(\\) expected a Point geometry, found LineString",
    ):
        tg_z(shapely.to_wkt(LINE_A))

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_z(None)


def test_tg_m():
    tg_m = lambda *args: db.execute("select tg_m(?)", args).fetchone()[0]
    # Same NULL-for-absent convention as tg_z().
    assert tg_m("POINT (1 2)") is None
    assert tg_m("POINT Z (1 2 3)") is None
    assert db.execute("select tg_m(tg_point(1, 2, 3, 4))").fetchone()[0] == 4
    assert tg_m("POINT ZM (1 2 3 4)") == 4

    with pytest.raises(
        sqlite3.OperationalError,
        match="tg_m\\(\\) expected a Point geometry, found LineString",
    ):
        tg_m(shapely.to_wkt(LINE_A))

    with pytest.raises(
        sqlite3.OperationalError,
        match="invalid geometry input",
    ):
        tg_m(None)


def test_tg_points_each(snapshot):
    assert (
        execute_all(
            db,
            "select rowid, point, tg_to_wkt(point), tg_to_geojson(point) from tg_points_each(?)",
            ["MULTIPOINT ((10 40), (40 30), (20 20), (30 10))"],
        )
        == snapshot
    )
    assert (
        execute_all(
            db,
            """
              select
                tg_points_each.rowid,
                point,
                tg_to_wkt(point),
                tg_to_geojson(point)
              from json_each(?)
              join tg_points_each(value)
            """,
            [
                json.dumps(
                    [
                        "MULTIPOINT ((10 20), (30 40))",
                        "MULTIPOINT (90 80,70 60,50 40)",
                    ]
                )
            ],
        )
        == snapshot
    )


def test_tg_group_multipoint():
    points = [x for x in range(10)]
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_multipoint(tg_point(value, value))) from json_each(?)",
            [json.dumps(points)],
        ).fetchone()[0]
        == "MULTIPOINT(0 0,1 1,2 2,3 3,4 4,5 5,6 6,7 7,8 8,9 9)"
    )

    with pytest.raises(
        sqlite3.OperationalError, match="invalid geometry input. Must be"
    ):
        db.execute("select tg_group_multipoint(NULL)")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("parameters to tg_group_multipoint() must be Point gemetries"),
    ):
        db.execute("select tg_group_multipoint('MULTIPOINT EMPTY')")


def test_tg_group_line():
    # aggregate input order is only defined with an explicit order by
    # (inside the aggregate call, or via an ordered subquery)
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_line(tg_point(value ->> 0, value ->> 1))) "
            "from json_each(?)",
            [json.dumps([[1, 1], [2, 2]])],
        ).fetchone()[0]
        == "LINESTRING(1 1,2 2)"
    )

    # zero rows: mirrors tg_group_multipoint()'s empty-aggregate behavior
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_line(tg_point(1, 1))) from (select 1 where 0)"
        ).fetchone()[0]
        == "LINESTRING EMPTY"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("parameters to tg_group_line() must be Point geometries"),
    ):
        db.execute(
            "select tg_group_line(value) from json_each(?)",
            [json.dumps(["LINESTRING(0 0, 1 1)"])],
        )


def test_tg_group_ring():
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_ring(tg_point(value ->> 0, value ->> 1))) "
            "from json_each(?)",
            [json.dumps([[0, 0], [10, 0], [10, 10]])],
        ).fetchone()[0]
        == "POLYGON((0 0,10 0,10 10))"
    )

    # zero rows: mirrors tg_ring()'s zero-arg empty-ring shape
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_ring(tg_point(1, 1))) from (select 1 where 0)"
        ).fetchone()[0]
        == "POLYGON(())"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("parameters to tg_group_ring() must be Point geometries"),
    ):
        db.execute(
            "select tg_group_ring(value) from json_each(?)",
            [json.dumps(["LINESTRING(0 0, 1 1)"])],
        )


def test_tg_group_polygon():
    # 1-arg form: first row is the exterior, remaining rows are holes
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_polygon(value)) from json_each(?)",
            [
                json.dumps(
                    [
                        "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))",
                        "POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))",
                    ]
                )
            ],
        ).fetchone()[0]
        == "POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))"
    )

    # bare ring rows (e.g. from tg_group_ring()) are accepted too
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_polygon("
            "tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 0))"
            "))"
        ).fetchone()[0]
        == "POLYGON((0 0,10 0,10 10,0 0))"
    )

    # zero rows: mirrors tg_polygon()'s zero-arg empty-polygon shape
    assert (
        db.execute(
            "select tg_to_wkt(tg_group_polygon(tg_ring(tg_point(1, 1)))) "
            "from (select 1 where 0)"
        ).fetchone()[0]
        == "POLYGON EMPTY"
    )

    # a Polygon WITH holes cannot be used as a ring row: silently using its
    # exterior would drop the holes' data
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_group_polygon() expected a ring, found a "
            "Polygon with holes (its holes would be silently dropped)"
        ),
    ):
        db.execute(
            "select tg_group_polygon(value) from json_each(?)",
            [
                json.dumps(
                    [
                        "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), "
                        "(1 1, 2 1, 2 2, 1 2, 1 1))"
                    ]
                )
            ],
        )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("argument to tg_group_polygon() expected a ring, found Point"),
    ):
        db.execute(
            "select tg_group_polygon(value) from json_each(?)",
            [json.dumps(["POINT(1 1)"])],
        )


def test_tg_group_geometrycollection():
    # tg_group_geometrycollection() is the primary (underscore-free) name;
    # tg_group_geometry_collection() is a deprecated compatibility alias —
    # same underlying step/final pair, so both must behave identically.
    for fn in ("tg_group_geometrycollection", "tg_group_geometry_collection"):
        assert (
            db.execute(
                f"select tg_to_wkt({fn}(value)) from json_each(?)",
                [json.dumps(["POINT(1 1)", "LINESTRING(0 0, 1 1)"])],
            ).fetchone()[0]
            == "GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))"
        )

        assert (
            db.execute(
                f"select tg_to_wkt({fn}(value)) from json_each(?)", [json.dumps([])]
            ).fetchone()[0]
            == "GEOMETRYCOLLECTION EMPTY"
        )


def test_tg_multipoint():
    tg_multipoint = lambda *args: db.execute(
        f"select tg_to_wkt(tg_multipoint({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_multipoint() == "MULTIPOINT EMPTY"
    assert tg_multipoint("point(0 0)") == "MULTIPOINT(0 0)"
    assert tg_multipoint("point(0 0)", "point(1 1)") == "MULTIPOINT(0 0,1 1)"
    assert (
        tg_multipoint("point(0 0)", "point(1 1)", "point(2 2)")
        == "MULTIPOINT(0 0,1 1,2 2)"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multipoint() at index 0 is an invalid geometry"
        ),
    ):
        tg_multipoint("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multipoint() at index 1 is an invalid geometry"
        ),
    ):
        tg_multipoint("point(1 1)", "invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multipoint() at index 0 expected a point, found MultiPoint"
        ),
    ):
        tg_multipoint("multipoint(1 1)")


def test_tg_linestring():
    # alias of tg_line(): same registered C function, so error messages
    # still say "tg_line()" (the name is baked into the impl, not looked up
    # from the SQL function name it was invoked as).
    tg_linestring = lambda *args: db.execute(
        f"select tg_to_wkt(tg_linestring({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_linestring() == "LINESTRING EMPTY"
    assert (
        tg_linestring("point(0 0)", "point(1 1)") == "LINESTRING(0 0,1 1)"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_line() at index 1 is an invalid geometry"
        ),
    ):
        tg_linestring("point(1 1)", "invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_line() at index 0 expected a point, found LineString"
        ),
    ):
        tg_linestring("LINESTRING EMPTY")


def test_tg_ring():
    tg_ring = lambda *args: db.execute(
        f"select tg_to_wkt(tg_ring({spread_args(args)}))", args
    ).fetchone()[0]
    # tg_ring_new() does not synthesize a closing point: WKT echoes back
    # exactly the points given, even though the ring is treated as
    # geometrically closed either way (see comment above tg_ring() in
    # sqlite-tg.c).
    assert tg_ring() == "POLYGON(())"
    assert tg_ring("point(0 0)", "point(10 0)", "point(10 10)") == (
        "POLYGON((0 0,10 0,10 10))"
    )
    assert tg_ring(
        "point(0 0)", "point(10 0)", "point(10 10)", "point(0 0)"
    ) == "POLYGON((0 0,10 0,10 10,0 0))"

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("argument to tg_ring() at index 0 is an invalid geometry"),
    ):
        tg_ring("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_ring() at index 1 expected a point, found LineString"
        ),
    ):
        tg_ring("point(0 0)", "LINESTRING(0 0, 1 1)")


def test_tg_polygon():
    tg_polygon = lambda *args: db.execute(
        f"select tg_to_wkt(tg_polygon({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_polygon() == "POLYGON EMPTY"

    exterior = "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))"
    hole = "POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))"

    assert tg_polygon(exterior) == "POLYGON((0 0,10 0,10 10,0 10,0 0))"
    assert tg_polygon(exterior, hole) == (
        "POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))"
    )

    # a Polygon-typed exterior arg (no holes) is accepted, same as a bare
    # ring from tg_ring()/tg_poly_exterior()
    assert db.execute(
        "select tg_to_wkt(tg_polygon(tg_ring(tg_point(0,0), tg_point(1,0), "
        "tg_point(1,1), tg_point(0,0))))"
    ).fetchone()[0] == "POLYGON((0 0,1 0,1 1,0 0))"

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_polygon() at index 0 is an invalid geometry"
        ),
    ):
        tg_polygon("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_polygon() at index 1 expected a ring, found Point"
        ),
    ):
        tg_polygon(exterior, "POINT(1 1)")

    # a Polygon WITH holes cannot be used as a ring argument: silently using
    # its exterior would drop the holes' data
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_polygon() at index 0 expected a ring, found a "
            "Polygon with holes (its holes would be silently dropped)"
        ),
    ):
        tg_polygon(
            "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))"
        )


def test_tg_rect():
    # 4-scalar form
    assert (
        db.execute("select tg_to_wkt(tg_rect(0, 0, 1, 1))").fetchone()[0]
        == "POLYGON((0 0,1 0,1 1,0 1,0 0))"
    )
    # counter-clockwise winding, closing point repeated
    assert (
        db.execute("select tg_to_wkt(tg_rect(0, 0, 10, 5))").fetchone()[0]
        == "POLYGON((0 0,10 0,10 5,0 5,0 0))"
    )

    # 2-point form, incl. pointer inputs
    assert (
        db.execute(
            "select tg_to_wkt(tg_rect(tg_point(0, 0), tg_point(1, 1)))"
        ).fetchone()[0]
        == "POLYGON((0 0,1 0,1 1,0 1,0 0))"
    )
    assert (
        db.execute(
            "select tg_to_wkt(tg_rect('POINT(0 0)', 'POINT(1 1)'))"
        ).fetchone()[0]
        == "POLYGON((0 0,1 0,1 1,0 1,0 0))"
    )

    # no normalization: minX > maxX / minY > maxY is passed through as given
    assert (
        db.execute("select tg_to_wkt(tg_rect(1, 1, 0, 0))").fetchone()[0]
        == "POLYGON((1 1,0 1,0 0,1 0,1 1))"
    )

    # composes with predicates like any other geometry
    assert (
        db.execute(
            "select tg_intersects(tg_rect(0, 0, 10, 10), tg_point(5, 5))"
        ).fetchone()[0]
        == 1
    )

    # error cases: wrong scalar arg type
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("rect minX value must be an integer or float"),
    ):
        db.execute("select tg_rect('a', 0, 1, 1)")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("rect maxY value must be an integer or float"),
    ):
        db.execute("select tg_rect(0, 0, 1, 'a')")

    # error cases: 2-arg form requires Point geometries
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("argument to tg_rect() at index 0 is an invalid geometry"),
    ):
        db.execute("select tg_rect('not a geometry', tg_point(1, 1))")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_rect() at index 1 expected a point, found LineString"
        ),
    ):
        db.execute(
            "select tg_rect(tg_point(0, 0), 'LINESTRING(0 0, 1 1)')"
        )


def test_tg_segment():
    # 4-scalar form
    assert (
        db.execute("select tg_to_wkt(tg_segment(0, 0, 1, 1))").fetchone()[0]
        == "LINESTRING(0 0,1 1)"
    )

    # 2-point form, incl. pointer inputs
    assert (
        db.execute(
            "select tg_to_wkt(tg_segment(tg_point(0, 0), tg_point(1, 1)))"
        ).fetchone()[0]
        == "LINESTRING(0 0,1 1)"
    )
    assert (
        db.execute(
            "select tg_to_wkt(tg_segment('POINT(0 0)', 'POINT(1 1)'))"
        ).fetchone()[0]
        == "LINESTRING(0 0,1 1)"
    )

    # composes with predicates like any other geometry
    assert (
        db.execute(
            "select tg_intersects(tg_segment(0, 0, 2, 2), tg_segment(0, 2, 2, 0))"
        ).fetchone()[0]
        == 1
    )

    # error cases: wrong scalar arg type
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("segment aX value must be an integer or float"),
    ):
        db.execute("select tg_segment('a', 0, 1, 1)")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("segment bY value must be an integer or float"),
    ):
        db.execute("select tg_segment(0, 0, 1, 'a')")

    # error cases: 2-arg form requires Point geometries
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_segment() at index 0 is an invalid geometry"
        ),
    ):
        db.execute("select tg_segment('not a geometry', tg_point(1, 1))")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_segment() at index 1 expected a point, found LineString"
        ),
    ):
        db.execute(
            "select tg_segment(tg_point(0, 0), 'LINESTRING(0 0, 1 1)')"
        )


def test_tg_multilinestring():
    tg_multilinestring = lambda *args: db.execute(
        f"select tg_to_wkt(tg_multilinestring({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_multilinestring() == "MULTILINESTRING EMPTY"
    assert tg_multilinestring("LINESTRING(0 0, 1 1)") == "MULTILINESTRING((0 0,1 1))"
    assert (
        tg_multilinestring("LINESTRING(0 0, 1 1)", "LINESTRING(2 2, 3 3)")
        == "MULTILINESTRING((0 0,1 1),(2 2,3 3))"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multilinestring() at index 0 is an invalid geometry"
        ),
    ):
        tg_multilinestring("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multilinestring() at index 1 expected a LineString, found Point"
        ),
    ):
        tg_multilinestring("LINESTRING(0 0, 1 1)", "POINT(1 1)")


def test_tg_multipolygon():
    tg_multipolygon = lambda *args: db.execute(
        f"select tg_to_wkt(tg_multipolygon({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_multipolygon() == "MULTIPOLYGON EMPTY"
    p1 = "POLYGON((0 0, 1 0, 1 1, 0 0))"
    p2 = "POLYGON((10 10, 11 10, 11 11, 10 10))"
    assert tg_multipolygon(p1) == "MULTIPOLYGON(((0 0,1 0,1 1,0 0)))"
    assert (
        tg_multipolygon(p1, p2)
        == "MULTIPOLYGON(((0 0,1 0,1 1,0 0)),((10 10,11 10,11 11,10 10)))"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multipolygon() at index 0 is an invalid geometry"
        ),
    ):
        tg_multipolygon("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_multipolygon() at index 1 expected a Polygon, found Point"
        ),
    ):
        tg_multipolygon(p1, "POINT(1 1)")


def test_tg_geometrycollection():
    tg_geometrycollection = lambda *args: db.execute(
        f"select tg_to_wkt(tg_geometrycollection({spread_args(args)}))", args
    ).fetchone()[0]
    assert tg_geometrycollection() == "GEOMETRYCOLLECTION EMPTY"
    assert (
        tg_geometrycollection("POINT(1 1)")
        == "GEOMETRYCOLLECTION(POINT(1 1))"
    )
    assert (
        tg_geometrycollection("POINT(1 1)", "LINESTRING(0 0, 1 1)")
        == "GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))"
    )
    # mixed types, including a polygon with a hole, and nested collections
    assert (
        tg_geometrycollection(
            "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))",
            "GEOMETRYCOLLECTION(POINT(2 2))",
        )
        == "GEOMETRYCOLLECTION(POLYGON((0 0,10 0,10 10,0 10,0 0),"
        "(1 1,2 1,2 2,1 2,1 1)),GEOMETRYCOLLECTION(POINT(2 2)))"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_geometrycollection() at index 0 is an invalid geometry"
        ),
    ):
        tg_geometrycollection("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_geometrycollection() at index 1 is an invalid geometry"
        ),
    ):
        tg_geometrycollection("POINT(1 1)", "invalid")


@pytest.mark.skip(reason="TODO")
def test_tg_contains():
    tg_contains = lambda *args: db.execute("select tg_contains(?)", args).fetchone()[0]

    pass


@pytest.mark.skip(reason="TODO")
def test_tg_coveredby():
    tg_coveredby = lambda *args: db.execute("select tg_coveredby(?)", args).fetchone()[
        0
    ]

    pass


@pytest.mark.skip(reason="TODO")
def test_tg_covers():
    tg_covers = lambda *args: db.execute("select tg_covers(?)", args).fetchone()[0]

    pass


@pytest.mark.skip(reason="TODO")
def test_tg_disjoint():
    tg_disjoint = lambda *args: db.execute("select tg_disjoint(?)", args).fetchone()[0]
    pass


@pytest.mark.skip(reason="TODO")
def test_tg_touches():
    tg_touches = lambda *args: db.execute("select tg_touches(?)", args).fetchone()[0]
    pass


@pytest.mark.skip(reason="TODO")
def test_tg_within():
    tg_within = lambda *args: db.execute("select tg_within(?)", args).fetchone()[0]
    pass


# ---------------------------------------------------------------------------
# tg0 constructor (typed-column syntax): CREATE-level coverage only. Write and
# read coverage returns when the new shadow-table layout and read/write paths
# land in follow-up changes.
# ---------------------------------------------------------------------------


def tg0_error(sql):
    with pytest.raises(sqlite3.OperationalError) as excinfo:
        db.execute(sql)
    return str(excinfo.value)


def table_xinfo(table):
    return execute_all(db, f"select * from pragma_table_xinfo('{table}')")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_geometry_columns(snapshot):
    db.execute(
        """
        create virtual table tg_geoms using tg0(
          a geometry,
          b point,
          c linestring,
          d polygon,
          e multipoint,
          f multilinestring,
          g multipolygon,
          h geometrycollection
        )
        """
    )
    assert table_xinfo("tg_geoms") == snapshot(name="every geometry type xinfo")
    db.execute("drop table tg_geoms")

    # indexing= options parse on geometry columns, keywords are
    # case-insensitive
    db.execute(
        "create virtual table tg_ix using tg0("
        "a geometry indexing=none, "
        "b polygon indexing=natural, "
        "c polygon indexing=ystripes, "
        "d POLYGON INDEXING=YSTRIPES)"
    )
    assert table_xinfo("tg_ix") == snapshot(name="indexing options xinfo")
    db.execute("drop table tg_ix")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_data_columns(snapshot):
    db.execute(
        "create virtual table tg_data using tg0("
        "shape point, a text, b int, c integer, d float, e double, f blob)"
    )
    assert table_xinfo("tg_data") == snapshot(name="every data type xinfo")
    db.execute("drop table tg_data")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_primary_keys(snapshot):
    db.execute(
        "create virtual table tg_ipk using tg0(id integer primary key, shape point)"
    )
    assert table_xinfo("tg_ipk") == snapshot(name="integer pk xinfo")
    # integer primary keys keep rowid tables: `rowid` still resolves
    db.execute("explain query plan select rowid from tg_ipk")
    db.execute("drop table tg_ipk")

    db.execute(
        "create virtual table tg_tpk using tg0(id text primary key, shape point)"
    )
    assert table_xinfo("tg_tpk") == snapshot(name="text pk xinfo")
    # text primary keys declare WITHOUT ROWID: no rowid column exists
    with pytest.raises(sqlite3.OperationalError, match="no such column: rowid"):
        db.execute("explain query plan select rowid from tg_tpk")
    db.execute("drop table tg_tpk")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_free_form_order(snapshot):
    # declaration order is free-form: data columns may come before geometry
    # columns, and a primary key declared mid-list is hoisted to column 0
    db.execute(
        "create virtual table tg_order using tg0("
        "label text, "
        "id integer primary key, "
        "boundary polygon indexing=ystripes, "
        "height double, "
        "center point)"
    )
    assert table_xinfo("tg_order") == snapshot(name="free-form order xinfo")
    db.execute("drop table tg_order")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_errors(snapshot):
    cases = {
        "no columns": "create virtual table tg_err using tg0()",
        "no geometry column": "create virtual table tg_err using tg0(label text)",
        "no geometry column pk only": (
            "create virtual table tg_err using tg0(id integer primary key)"
        ),
        "could not parse": "create virtual table tg_err using tg0(a b c)",
        "unknown column type": "create virtual table tg_err using tg0(a geometri)",
        "more than one primary key": (
            "create virtual table tg_err using tg0("
            "id integer primary key, id2 text primary key, shape point)"
        ),
        "too many geometry columns": (
            "create virtual table tg_err using tg0("
            + ", ".join(f"g{i} point" for i in range(9))
            + ")"
        ),
        "too many data columns": (
            "create virtual table tg_err using tg0(shape point, "
            + ", ".join(f"d{i} text" for i in range(17))
            + ")"
        ),
        "duplicate column name": (
            "create virtual table tg_err using tg0(a point, A point)"
        ),
        "duplicate column name with pk": (
            "create virtual table tg_err using tg0(a integer primary key, a point)"
        ),
        "reserved column name distance": (
            "create virtual table tg_err using tg0(distance point)"
        ),
        "reserved column name rowid": (
            "create virtual table tg_err using tg0(shape point, rowid text)"
        ),
        "unknown indexing option": (
            "create virtual table tg_err using tg0(a polygon indexing=foo)"
        ),
        "unknown geometry option": (
            "create virtual table tg_err using tg0(a polygon compression=zstd)"
        ),
        "indexing on data column": (
            "create virtual table tg_err using tg0("
            "shape point, label text indexing=none)"
        ),
        "indexing on primary key": (
            "create virtual table tg_err using tg0("
            "id integer primary key indexing=none, shape point)"
        ),
    }
    for name, sql in cases.items():
        assert tg0_error(sql) == snapshot(name=name)


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_old_syntax_migration_hint(snapshot):
    # pre-typed-column syntax: bare identifiers became aux columns next to an
    # implicit _shape column. Bare identifiers now get a targeted migration
    # hint on top of the could-not-parse error.
    assert tg0_error("create virtual table tg_old using tg0(tzid)") == snapshot(
        name="bare identifier hint"
    )


# ---------------------------------------------------------------------------
# tg0 shadow tables: t_info, t_content, one t_rtreeNN per geometry column,
# and t_rowids for text primary keys.
# ---------------------------------------------------------------------------


def shadow_tables(connection, table):
    # sqlite_master rows for `<table>_*` shadow tables (including the rtree
    # module's own _node/_parent/_rowid tables underneath each bbox rtree).
    # LIKE-escape the underscores so they don't match any character.
    pattern = table.replace("_", "\\_") + "\\_%"
    return execute_all(
        connection,
        "select name, type from sqlite_master where name like ? escape '\\' order by name",
        [pattern],
    )


def table_sql(name):
    return db.execute(
        "select sql from sqlite_master where name = ?", [name]
    ).fetchone()[0]


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_shadow_tables_no_pk(snapshot):
    db.execute("create virtual table tg_shadow using tg0(shape geometry)")
    assert shadow_tables(db, "tg_shadow") == snapshot(name="no pk shadow tables")
    assert table_sql("tg_shadow_content") == snapshot(name="no pk content sql")
    assert table_sql("tg_shadow_info") == snapshot(name="no pk info sql")
    db.execute("drop table tg_shadow")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_shadow_tables_integer_pk(snapshot):
    # integer primary keys alias the content table's rowid: no extra shadow
    # table beyond the no-pk layout
    db.execute(
        "create virtual table tg_shadow_ipk using tg0("
        "id integer primary key, shape point, label text)"
    )
    assert shadow_tables(db, "tg_shadow_ipk") == snapshot(
        name="integer pk shadow tables"
    )
    assert table_sql("tg_shadow_ipk_content") == snapshot(name="integer pk content sql")
    db.execute("drop table tg_shadow_ipk")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_shadow_tables_text_pk(snapshot):
    # text primary keys additionally get a t_rowids mapping table that assigns
    # the int64 rowids the rtrees and content table are keyed by
    db.execute(
        "create virtual table tg_shadow_tpk using tg0("
        "id text primary key, shape point, label text)"
    )
    assert shadow_tables(db, "tg_shadow_tpk") == snapshot(name="text pk shadow tables")
    assert table_sql("tg_shadow_tpk_content") == snapshot(name="text pk content sql")
    assert table_sql("tg_shadow_tpk_rowids") == snapshot(name="text pk rowids sql")
    db.execute("drop table tg_shadow_tpk")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_shadow_tables_multi_geometry(snapshot):
    # one bbox rtree per geometry column; geomNN/dataNN numbering follows the
    # declaration-kind order, independent of how they interleave
    db.execute(
        "create virtual table tg_shadow_multi using tg0("
        "center point, label text, boundary polygon, height float, extent geometry)"
    )
    assert shadow_tables(db, "tg_shadow_multi") == snapshot(
        name="multi geometry shadow tables"
    )
    assert table_sql("tg_shadow_multi_content") == snapshot(
        name="multi geometry content sql"
    )
    db.execute("drop table tg_shadow_multi")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_info_create_version():
    db.execute("create virtual table tg_shadow_v using tg0(shape point)")
    version = db.execute("select tg_version()").fetchone()[0]
    assert execute_all(db, "select key, value from tg_shadow_v_info") == [
        {"key": "CREATE_VERSION", "value": version}
    ]
    db.execute("drop table tg_shadow_v")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_reopen_connects(tmp_path, snapshot):
    # CREATE persists a table that xConnect (no DDL) can re-open from disk
    path = str(tmp_path / "tg0.db")
    db1 = connect(EXT_PATH, path)
    db1.execute(
        "create virtual table tg_reopen using tg0("
        "id text primary key, shape point, label text)"
    )
    db1.commit()
    db1.close()

    db2 = connect(EXT_PATH, path)
    assert execute_all(
        db2, "select * from pragma_table_xinfo('tg_reopen')"
    ) == snapshot(name="reopened table_xinfo")
    assert shadow_tables(db2, "tg_reopen") == snapshot(name="reopened shadow tables")
    db2.close()


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_drop_removes_shadow_tables(snapshot):
    # the widest layout: text pk (rowids) + multiple geometry columns (rtrees)
    db.execute(
        "create virtual table tg_dropme using tg0("
        "id text primary key, center point, boundary polygon, label text)"
    )
    assert shadow_tables(db, "tg_dropme") == snapshot(name="before drop")
    db.execute("drop table tg_dropme")
    assert shadow_tables(db, "tg_dropme") == []
    assert (
        execute_all(
            db,
            "select name from sqlite_master where name like 'tg\\_dropme%' escape '\\'",
        )
        == []
    )


@pytest.mark.skip(reason="TODO not needeD?")
def test_coverage():
    current_module = inspect.getmodule(inspect.currentframe())
    test_methods = [
        member[0]
        for member in inspect.getmembers(current_module)
        if member[0].startswith("test_")
    ]
    funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
    README = (Path(__file__).parent.parent / "docs.md").read_text()
    for func in [*FUNCTIONS, *MODULES]:
        assert func in funcs_with_tests, f"{func} is not tested"
        assert func in README, f"{func} is not documented"
        assert f'name="{func}"' in README, f"{func} is not documented"
        assert f"{func}(" in README, f"{func} missing code sample"


@pytest.mark.skip(reason="TODO not needeD?")
def test_coverage_new():
    current_module = inspect.getmodule(inspect.currentframe())
    test_methods = [
        member[0]
        for member in inspect.getmembers(current_module)
        if member[0].startswith("test_")
    ]
    funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
    import yaml

    ref = yaml.safe_load((Path(__file__).parent.parent / "reference.yaml").read_text())
    for func in FUNCTIONS:
        func_ref = ref.get("functions").get(func)
        assert func_ref, f"{func} is not in reference.yaml"
        for field in ["desc", "example"]:
            assert field in func_ref and func_ref.get(
                field
            ), f"{func} reference is missing {field}"
    for module in MODULES:
        m_ref = ref.get("table_functions").get(module) or ref.get("virtual_tables").get(
            module
        )
        print(ref.get("virtual_tables").get(module))
        assert m_ref, f"{module} is not in reference.yaml"
        for field in ["desc", "example"]:
            assert field in m_ref and m_ref.get(
                field
            ), f"{module} reference is missing {field}"
    README = (Path(__file__).parent.parent / "docs.md").read_text()
    for func in [*FUNCTIONS, *MODULES]:
        assert func in funcs_with_tests, f"{func} is not tested"
        assert func in README, f"{func} is not documented"
        assert f'name="{func}"' in README, f"{func} is not documented"
        assert f"{func}(" in README, f"{func} missing code sample"


if __name__ == "__main__":
    unittest.main()

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
    tg_is_feature = lambda *args: db.execute(
        "select tg_is_feature(?)", args
    ).fetchone()[0]
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
    assert tg_is_featurecollection('{"type":"FeatureCollection","features":[]}') == 1
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
    assert tg_line_points_each("LINESTRING (10 10, 20 20, 10 40, 40 40)") == snapshot

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
    assert tg_line_segments_each("LINESTRING (10 10, 20 20, 10 40, 40 40)") == snapshot

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
    assert tg_linestring("point(0 0)", "point(1 1)") == "LINESTRING(0 0,1 1)"

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("argument to tg_line() at index 1 is an invalid geometry"),
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
    assert (
        tg_ring("point(0 0)", "point(10 0)", "point(10 10)", "point(0 0)")
        == "POLYGON((0 0,10 0,10 10,0 0))"
    )

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
    assert (
        db.execute(
            "select tg_to_wkt(tg_polygon(tg_ring(tg_point(0,0), tg_point(1,0), "
            "tg_point(1,1), tg_point(0,0))))"
        ).fetchone()[0]
        == "POLYGON((0 0,1 0,1 1,0 0))"
    )

    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape("argument to tg_polygon() at index 0 is an invalid geometry"),
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
        tg_polygon("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))")


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
        db.execute("select tg_to_wkt(tg_rect('POINT(0 0)', 'POINT(1 1)'))").fetchone()[
            0
        ]
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
        db.execute("select tg_rect(tg_point(0, 0), 'LINESTRING(0 0, 1 1)')")


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
        match=re.escape("argument to tg_segment() at index 0 is an invalid geometry"),
    ):
        db.execute("select tg_segment('not a geometry', tg_point(1, 1))")
    with pytest.raises(
        sqlite3.OperationalError,
        match=re.escape(
            "argument to tg_segment() at index 1 expected a point, found LineString"
        ),
    ):
        db.execute("select tg_segment(tg_point(0, 0), 'LINESTRING(0 0, 1 1)')")


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
    assert tg_geometrycollection("POINT(1 1)") == "GEOMETRYCOLLECTION(POINT(1 1))"
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
# tg0 constructor (typed-column syntax): CREATE-level coverage. Write coverage
# lives in the "tg0 write path" section below, read coverage in the "tg0 read
# path" section after it.
# ---------------------------------------------------------------------------


def tg0_error(sql):
    with pytest.raises(sqlite3.OperationalError) as excinfo:
        db.execute(sql)
    return str(excinfo.value)


def table_xinfo(table):
    return execute_all(db, f"select * from pragma_table_xinfo('{table}')")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_create_geometry_columns(snapshot):
    db.execute("""
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
        """)
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


# ---------------------------------------------------------------------------
# tg0 write path: INSERT / DELETE / UPDATE. Round-trips are asserted against
# the shadow tables directly — t_content for stored WKB and data values,
# t_rtreeNN for bboxes, t_rowids for text primary key mappings — so write
# bugs can't hide behind the read path.
# ---------------------------------------------------------------------------


def wkb(geometry):
    return db.execute("select tg_to_wkb(?)", [geometry]).fetchone()[0]


def tg0_write_error(sql, args=None):
    # writes fail with SQLITE_CONSTRAINT (IntegrityError) or SQLITE_ERROR
    # (OperationalError); include the exception class so snapshots pin both
    with pytest.raises(sqlite3.DatabaseError) as excinfo:
        db.execute(sql, args or [])
    return f"{type(excinfo.value).__name__}: {excinfo.value}"


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_insert_formats():
    # every geomValue() input coercion round-trips into the same stored WKB
    db.execute("create virtual table tg_wr using tg0(shape geometry)")
    db.execute("insert into tg_wr(shape) values (?)", ["POINT(1 2)"])
    db.execute(
        "insert into tg_wr(shape) values (?)",
        ['{"type": "Point", "coordinates": [3, 4]}'],
    )
    db.execute("insert into tg_wr(shape) values (?)", [wkb("POINT(5 6)")])
    db.execute("insert into tg_wr(shape) values (tg_point(7, 8))")
    assert execute_all(
        db, "select rowid, typeof(geom00) as type, geom00 from tg_wr_content"
    ) == [
        {"rowid": 1, "type": "blob", "geom00": wkb("POINT(1 2)")},
        {"rowid": 2, "type": "blob", "geom00": wkb("POINT(3 4)")},
        {"rowid": 3, "type": "blob", "geom00": wkb("POINT(5 6)")},
        {"rowid": 4, "type": "blob", "geom00": wkb("POINT(7 8)")},
    ]
    assert execute_all(
        db, "select id, minX, maxX, minY, maxY from tg_wr_rtree00 order by id"
    ) == [
        {"id": 1, "minX": 1.0, "maxX": 1.0, "minY": 2.0, "maxY": 2.0},
        {"id": 2, "minX": 3.0, "maxX": 3.0, "minY": 4.0, "maxY": 4.0},
        {"id": 3, "minX": 5.0, "maxX": 5.0, "minY": 6.0, "maxY": 6.0},
        {"id": 4, "minX": 7.0, "maxX": 7.0, "minY": 8.0, "maxY": 8.0},
    ]
    db.execute("drop table tg_wr")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_insert_wkb_verbatim():
    import struct

    # WKB blob input is stored byte-identical: a big-endian WKB point would
    # come back little-endian if it were re-serialized through tg_geom_wkb
    big_endian = b"\x00" + struct.pack(">I", 1) + struct.pack(">dd", 5.0, 6.0)
    db.execute("create virtual table tg_wv using tg0(shape point)")
    db.execute("insert into tg_wv(shape) values (?)", [big_endian])
    stored = db.execute("select geom00 from tg_wv_content").fetchone()[0]
    assert stored == big_endian
    assert stored != wkb("POINT(5 6)")  # tg's own serialization differs
    # the bbox was still parsed out of the big-endian bytes
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_wv_rtree00") == [
        {"id": 1, "minX": 5.0, "maxX": 5.0, "minY": 6.0, "maxY": 6.0}
    ]
    db.execute("drop table tg_wv")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_insert_null_geometry(snapshot):
    db.execute("create virtual table tg_wn using tg0(shape point, label text)")
    assert tg0_write_error(
        "insert into tg_wn(shape, label) values (NULL, 'a')"
    ) == snapshot(name="explicit NULL geometry")
    # an omitted geometry column arrives as NULL too
    assert tg0_write_error("insert into tg_wn(label) values ('a')") == snapshot(
        name="omitted geometry"
    )
    assert execute_all(db, "select count(*) as n from tg_wn_content") == [{"n": 0}]
    db.execute("drop table tg_wn")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_geometry_type_enforcement(snapshot):
    # narrowed columns enforce tg_geom_typeof exactly: no Polygon→MultiPolygon
    # promotion (decision 12)
    db.execute("create virtual table tg_wt using tg0(boundary multipolygon)")
    assert tg0_write_error(
        "insert into tg_wt(boundary) values ('POLYGON((0 0, 1 0, 1 1, 0 0))')"
    ) == snapshot(name="wrong subtype")
    assert tg0_write_error(
        "insert into tg_wt(boundary) values ('not a geometry')"
    ) == snapshot(name="unparseable input")
    db.execute(
        "insert into tg_wt(boundary) values ('MULTIPOLYGON(((0 0, 1 0, 1 1, 0 0)))')"
    )
    assert execute_all(db, "select count(*) as n from tg_wt_content") == [{"n": 1}]
    db.execute("drop table tg_wt")

    # a generic `geometry` column accepts all 7 tg types
    db.execute("create virtual table tg_wg using tg0(shape geometry)")
    for geometry in [
        "POINT(1 1)",
        "LINESTRING(0 0, 1 1)",
        "POLYGON((0 0, 1 0, 1 1, 0 0))",
        "MULTIPOINT(1 1, 2 2)",
        "MULTILINESTRING((0 0, 1 1))",
        "MULTIPOLYGON(((0 0, 1 0, 1 1, 0 0)))",
        "GEOMETRYCOLLECTION(POINT(1 1))",
    ]:
        db.execute("insert into tg_wg(shape) values (?)", [geometry])
    assert execute_all(db, "select count(*) as n from tg_wg_content") == [{"n": 7}]
    db.execute("drop table tg_wg")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_data_columns(snapshot):
    db.execute(
        "create virtual table tg_wd using tg0("
        "shape point, label text, n integer, height float, payload blob)"
    )
    db.execute(
        "insert into tg_wd(shape, label, n, height, payload) "
        "values ('POINT(1 1)', 'a', 1, 1.5, x'ff')"
    )
    # NULL is always allowed in data columns
    db.execute("insert into tg_wd(shape) values ('POINT(2 2)')")
    # integers are allowed into float columns, stored as floats
    db.execute("insert into tg_wd(shape, height) values ('POINT(3 3)', 7)")
    assert execute_all(
        db,
        "select rowid, data00, data01, data02, typeof(data02) as height_type, "
        "data03 from tg_wd_content",
    ) == [
        {
            "rowid": 1,
            "data00": "a",
            "data01": 1,
            "data02": 1.5,
            "height_type": "real",
            "data03": b"\xff",
        },
        {
            "rowid": 2,
            "data00": None,
            "data01": None,
            "data02": None,
            "height_type": "null",
            "data03": None,
        },
        {
            "rowid": 3,
            "data00": None,
            "data01": None,
            "data02": 7.0,
            "height_type": "real",
            "data03": None,
        },
    ]
    cases = {
        "integer into text": "insert into tg_wd(shape, label) values ('POINT(0 0)', 1)",
        "text into integer": "insert into tg_wd(shape, n) values ('POINT(0 0)', 'x')",
        "float into integer": "insert into tg_wd(shape, n) values ('POINT(0 0)', 1.5)",
        "text into float": "insert into tg_wd(shape, height) values ('POINT(0 0)', 'x')",
        "text into blob": "insert into tg_wd(shape, payload) values ('POINT(0 0)', 'x')",
    }
    for name, sql in cases.items():
        assert tg0_write_error(sql) == snapshot(name=name)
    assert execute_all(db, "select count(*) as n from tg_wd_content") == [{"n": 3}]
    db.execute("drop table tg_wd")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_last_insert_rowid():
    last = lambda: db.execute("select last_insert_rowid()").fetchone()[0]

    db.execute("create virtual table tg_wl using tg0(shape point)")
    db.execute("insert into tg_wl(rowid, shape) values (10, 'POINT(1 1)')")
    assert last() == 10
    # autoassigned rowids continue after the largest existing rowid
    db.execute("insert into tg_wl(shape) values ('POINT(2 2)')")
    assert last() == 11
    db.execute("drop table tg_wl")

    db.execute(
        "create virtual table tg_wli using tg0(id integer primary key, shape point)"
    )
    db.execute("insert into tg_wli(id, shape) values (99, 'POINT(1 1)')")
    assert last() == 99
    db.execute("insert into tg_wli(shape) values ('POINT(2 2)')")
    assert last() == 100
    assert execute_all(db, "select rowid from tg_wli_content order by rowid") == [
        {"rowid": 99},
        {"rowid": 100},
    ]
    db.execute("drop table tg_wli")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_text_pk(snapshot):
    db.execute("create virtual table tg_wp using tg0(id text primary key, shape point)")
    db.execute("insert into tg_wp(id, shape) values ('alpha', 'POINT(1 1)')")
    assert db.execute("select last_insert_rowid()").fetchone()[0] == 1
    # t_rowids assigns the int64 rowid the content table and rtree are keyed by
    assert execute_all(db, "select rowid, id from tg_wp_rowids") == [
        {"rowid": 1, "id": "alpha"}
    ]
    assert execute_all(db, "select rowid from tg_wp_content") == [{"rowid": 1}]
    assert execute_all(db, "select id from tg_wp_rtree00") == [{"id": 1}]

    assert tg0_write_error(
        "insert into tg_wp(id, shape) values ('alpha', 'POINT(2 2)')"
    ) == snapshot(name="duplicate id")
    assert tg0_write_error(
        "insert into tg_wp(shape) values ('POINT(2 2)')"
    ) == snapshot(name="NULL id")
    assert tg0_write_error(
        "insert into tg_wp(id, shape) values (5, 'POINT(2 2)')"
    ) == snapshot(name="non-text id")
    assert execute_all(db, "select count(*) as n from tg_wp_content") == [{"n": 1}]
    db.execute("drop table tg_wp")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_delete():
    db.execute("create virtual table tg_del using tg0(shape point, label text)")
    db.execute("insert into tg_del(shape, label) values ('POINT(1 1)', 'a')")
    db.execute("insert into tg_del(shape, label) values ('POINT(2 2)', 'b')")

    db.execute("delete from tg_del where rowid = 1")
    assert execute_all(db, "select rowid, data00 from tg_del_content") == [
        {"rowid": 2, "data00": "b"}
    ]
    assert execute_all(db, "select id from tg_del_rtree00") == [{"id": 2}]

    # fullscan DELETE (no WHERE) drains every shadow table
    db.execute("insert into tg_del(shape, label) values ('POINT(3 3)', 'c')")
    db.execute("delete from tg_del")
    assert execute_all(db, "select count(*) as n from tg_del_content") == [{"n": 0}]
    assert execute_all(db, "select count(*) as n from tg_del_rtree00") == [{"n": 0}]
    db.execute("drop table tg_del")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_delete_text_pk():
    db.execute(
        "create virtual table tg_delp using tg0(id text primary key, shape point)"
    )
    db.execute("insert into tg_delp(id, shape) values ('a', 'POINT(1 1)')")
    db.execute("insert into tg_delp(id, shape) values ('b', 'POINT(2 2)')")
    db.execute("delete from tg_delp where id = 'a'")
    # content, rtree, and the rowids mapping row are all gone
    assert execute_all(db, "select rowid, id from tg_delp_rowids") == [
        {"rowid": 2, "id": "b"}
    ]
    assert execute_all(db, "select rowid from tg_delp_content") == [{"rowid": 2}]
    assert execute_all(db, "select id from tg_delp_rtree00") == [{"id": 2}]
    db.execute("drop table tg_delp")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_update():
    db.execute("create virtual table tg_up using tg0(shape point, label text)")
    db.execute("insert into tg_up(shape, label) values ('POINT(1 1)', 'a')")

    # a geometry change rewrites the content WKB and moves the rtree bbox
    db.execute("update tg_up set shape = 'POINT(9 9)' where rowid = 1")
    assert execute_all(db, "select geom00 from tg_up_content") == [
        {"geom00": wkb("POINT(9 9)")}
    ]
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_up_rtree00") == [
        {"id": 1, "minX": 9.0, "maxX": 9.0, "minY": 9.0, "maxY": 9.0}
    ]

    # a data-only change leaves the geometry and its rtree bbox untouched.
    # This exercises the sqlite3_value_nochange() path: were the unchanged
    # geometry column treated as written, its no-change value would be
    # rejected as a NULL geometry and this UPDATE would fail.
    db.execute("update tg_up set label = 'z' where label = 'a'")
    assert execute_all(db, "select data00, geom00 from tg_up_content") == [
        {"data00": "z", "geom00": wkb("POINT(9 9)")}
    ]
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_up_rtree00") == [
        {"id": 1, "minX": 9.0, "maxX": 9.0, "minY": 9.0, "maxY": 9.0}
    ]

    # data columns can be UPDATEd to NULL; geometry columns cannot
    db.execute("update tg_up set label = NULL where rowid = 1")
    assert execute_all(db, "select data00 from tg_up_content") == [{"data00": None}]
    with pytest.raises(sqlite3.IntegrityError):
        db.execute("update tg_up set shape = NULL where rowid = 1")
    db.execute("drop table tg_up")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_update_text_pk():
    db.execute(
        "create virtual table tg_upp using tg0("
        "id text primary key, shape point, label text)"
    )
    db.execute("insert into tg_upp(id, shape, label) values ('a', 'POINT(1 1)', 'x')")
    db.execute("update tg_upp set shape = 'POINT(5 5)', label = 'y' where id = 'a'")
    assert execute_all(db, "select geom00, data00 from tg_upp_content") == [
        {"geom00": wkb("POINT(5 5)"), "data00": "y"}
    ]
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_upp_rtree00") == [
        {"id": 1, "minX": 5.0, "maxX": 5.0, "minY": 5.0, "maxY": 5.0}
    ]
    db.execute("drop table tg_upp")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_update_pk_change(snapshot):
    # changing the rowid or primary key of a row is unsupported (decision 14)
    db.execute("create virtual table tg_upk using tg0(shape point)")
    db.execute("insert into tg_upk(shape) values ('POINT(1 1)')")
    assert tg0_write_error("update tg_upk set rowid = 99 where rowid = 1") == snapshot(
        name="rowid change"
    )
    db.execute("drop table tg_upk")

    db.execute(
        "create virtual table tg_upki using tg0(id integer primary key, shape point)"
    )
    db.execute("insert into tg_upki(id, shape) values (1, 'POINT(1 1)')")
    assert tg0_write_error("update tg_upki set id = 99 where id = 1") == snapshot(
        name="integer pk change"
    )
    db.execute("drop table tg_upki")

    db.execute(
        "create virtual table tg_upkt using tg0(id text primary key, shape point)"
    )
    db.execute("insert into tg_upkt(id, shape) values ('a', 'POINT(1 1)')")
    assert tg0_write_error("update tg_upkt set id = 'b' where id = 'a'") == snapshot(
        name="text pk change"
    )
    db.execute("drop table tg_upkt")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_multi_geometry():
    db.execute("create virtual table tg_wm using tg0(center point, boundary polygon)")
    db.execute(
        "insert into tg_wm(center, boundary) "
        "values ('POINT(1 1)', 'POLYGON((0 0, 4 0, 4 4, 0 0))')"
    )
    # each geometry column's rtree holds that column's own bbox
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_wm_rtree00") == [
        {"id": 1, "minX": 1.0, "maxX": 1.0, "minY": 1.0, "maxY": 1.0}
    ]
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_wm_rtree01") == [
        {"id": 1, "minX": 0.0, "maxX": 4.0, "minY": 0.0, "maxY": 4.0}
    ]

    # updating one geometry column only moves that column's rtree bbox
    db.execute(
        "update tg_wm set boundary = 'POLYGON((5 5, 6 5, 6 6, 5 5))' where rowid = 1"
    )
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_wm_rtree00") == [
        {"id": 1, "minX": 1.0, "maxX": 1.0, "minY": 1.0, "maxY": 1.0}
    ]
    assert execute_all(db, "select id, minX, maxX, minY, maxY from tg_wm_rtree01") == [
        {"id": 1, "minX": 5.0, "maxX": 6.0, "minY": 5.0, "maxY": 6.0}
    ]
    db.execute("drop table tg_wm")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_insert_failure_atomicity():
    # a constraint on the 2nd geometry column of an INSERT leaves no partial
    # rows in any shadow table
    db.execute("create virtual table tg_wa using tg0(center point, boundary polygon)")
    with pytest.raises(sqlite3.IntegrityError):
        db.execute(
            "insert into tg_wa(center, boundary) values ('POINT(1 1)', 'POINT(2 2)')"
        )
    assert execute_all(
        db,
        "select (select count(*) from tg_wa_content) as content, "
        "(select count(*) from tg_wa_rtree00) as rtree00, "
        "(select count(*) from tg_wa_rtree01) as rtree01",
    ) == [{"content": 0, "rtree00": 0, "rtree01": 0}]
    db.execute("drop table tg_wa")

    # a duplicate text primary key leaves the existing row untouched and
    # writes nothing new
    db.execute(
        "create virtual table tg_wat using tg0(id text primary key, shape point)"
    )
    db.execute("insert into tg_wat(id, shape) values ('a', 'POINT(1 1)')")
    with pytest.raises(sqlite3.IntegrityError):
        db.execute("insert into tg_wat(id, shape) values ('a', 'POINT(2 2)')")
    assert execute_all(
        db,
        "select (select count(*) from tg_wat_content) as content, "
        "(select count(*) from tg_wat_rtree00) as rtree, "
        "(select count(*) from tg_wat_rowids) as rowids",
    ) == [{"content": 1, "rtree": 1, "rowids": 1}]
    db.execute("drop table tg_wat")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_write_distance_column(snapshot):
    # the hidden distance column is reserved for KNN and rejects writes
    db.execute("create virtual table tg_wdist using tg0(shape point)")
    assert tg0_write_error(
        "insert into tg_wdist(shape, distance) values ('POINT(1 1)', 1)"
    ) == snapshot(name="insert into distance")
    db.execute("drop table tg_wdist")


# ---------------------------------------------------------------------------
# tg0 read path: xBestIndex plans (fullscan / point / predicate) and all 8
# predicates, exact results driven by an rtree bbox prefilter + exact tg
# post-filter.
# ---------------------------------------------------------------------------

# Query geometry: the 10x10 square at the origin.
TG0_READ_QUERY = "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))"

# Fixture rows with known relationships to TG0_READ_QUERY.
TG0_READ_FIXTURE = [
    # small polygon strictly inside the query square
    ("in_poly", "POLYGON((2 2, 4 2, 4 4, 2 4, 2 2))"),
    # polygon touching the query square at exactly one corner, (10, 10)
    ("corner_poly", "POLYGON((10 10, 12 10, 12 12, 10 12, 10 10))"),
    # line crossing all the way through the query square
    ("cross_line", "LINESTRING(-2 5, 12 5)"),
    # polygon far away from the query square
    ("far_poly", "POLYGON((20 20, 30 20, 30 30, 20 30, 20 20))"),
    # polygon strictly containing the query square
    ("cover_poly", "POLYGON((-2 -2, 12 -2, 12 12, -2 12, -2 -2))"),
    # polygon exactly equal to the query square
    ("equal_poly", "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))"),
    # point strictly inside the query square
    ("in_point", "POINT(5 5)"),
    # point outside the query square
    ("out_point", "POINT(15 15)"),
    # point on the query square's boundary
    ("edge_point", "POINT(0 5)"),
    # one point inside the query square, one outside
    ("multi", "MULTIPOINT(1 1, 20 1)"),
]

# Hand-computed: which fixture rows relate to TG0_READ_QUERY under each
# predicate, per tg's semantics — tg_within/tg_contains are false on
# boundary-only contact (edge_point), tg_coveredby/tg_covers include it, and
# tg_touches needs a common point with disjoint interiors (corner_poly,
# edge_point).
TG0_READ_EXPECTED = {
    "tg_intersects": [
        "in_poly",
        "corner_poly",
        "cross_line",
        "cover_poly",
        "equal_poly",
        "in_point",
        "edge_point",
        "multi",
    ],
    "tg_disjoint": ["far_poly", "out_point"],
    "tg_contains": ["cover_poly", "equal_poly"],
    "tg_within": ["in_poly", "equal_poly", "in_point"],
    "tg_covers": ["cover_poly", "equal_poly"],
    "tg_coveredby": ["in_poly", "equal_poly", "in_point", "edge_point"],
    "tg_touches": ["corner_poly", "edge_point"],
    "tg_equals": ["equal_poly"],
}


@pytest.fixture
def tg0_read_table():
    db.execute("create virtual table tg_rd using tg0(shape geometry, label text)")
    for label, wkt in TG0_READ_FIXTURE:
        db.execute("insert into tg_rd(shape, label) values (?, ?)", [wkt, label])
    yield "tg_rd"
    db.execute("drop table tg_rd")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_all_predicates(tg0_read_table):
    for predicate, expected in TG0_READ_EXPECTED.items():
        rows = db.execute(
            f"select label from tg_rd where {predicate}(shape, ?) order by rowid",
            [TG0_READ_QUERY],
        ).fetchall()
        assert [row["label"] for row in rows] == expected, predicate
        # cross-check against a manual scan that cannot be intercepted by
        # xBestIndex (the predicate's first argument is not a bare column)
        manual = db.execute(
            f"select label from tg_rd where {predicate}(tg_to_wkb(shape), ?) "
            "order by rowid",
            [TG0_READ_QUERY],
        ).fetchall()
        assert [row["label"] for row in manual] == expected, predicate


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_two_predicates(tg0_read_table):
    # two predicates in one WHERE clause: one is consumed for the rtree plan,
    # the other post-evaluates as a scalar function — results intersect
    rows = db.execute(
        "select label from tg_rd where tg_intersects(shape, ?) "
        "and tg_coveredby(shape, ?) order by rowid",
        [TG0_READ_QUERY, TG0_READ_QUERY],
    ).fetchall()
    assert [row["label"] for row in rows] == TG0_READ_EXPECTED["tg_coveredby"]
    # disjoint + a non-disjoint predicate: the non-disjoint one drives the
    # rtree; disjoint-from-far_poly then filters far_poly's own row out
    rows = db.execute(
        "select label from tg_rd where tg_disjoint(shape, ?) "
        "and tg_intersects(shape, ?) order by rowid",
        ["POLYGON((20 20, 30 20, 30 30, 20 30, 20 20))", TG0_READ_QUERY],
    ).fetchall()
    assert [row["label"] for row in rows] == TG0_READ_EXPECTED["tg_intersects"]


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_data_column_predicate_not_intercepted(tg0_read_table):
    # a predicate on a data column is NOT intercepted by xBestIndex: it runs
    # as the plain scalar function, which errors on non-geometry input with
    # the exact same message
    with pytest.raises(sqlite3.OperationalError) as vtab_err:
        db.execute(
            "select label from tg_rd where tg_intersects(label, ?)",
            [TG0_READ_QUERY],
        ).fetchall()
    with pytest.raises(sqlite3.OperationalError) as scalar_err:
        db.execute("select tg_intersects('in_poly', ?)", [TG0_READ_QUERY]).fetchone()
    assert str(vtab_err.value) == str(scalar_err.value)


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_point_plans(tg0_read_table):
    row = db.execute("select rowid, label from tg_rd where rowid = 3").fetchone()
    assert (row["rowid"], row["label"]) == (3, "cross_line")

    db.execute(
        "create virtual table tg_rdp using tg0(id text primary key, shape point)"
    )
    db.execute("insert into tg_rdp(id, shape) values ('a', 'POINT(1 1)')")
    db.execute("insert into tg_rdp(id, shape) values ('b', 'POINT(2 2)')")
    rows = execute_all(
        db, "select id, tg_to_wkt(shape) as wkt from tg_rdp where id = 'b'"
    )
    assert rows == [{"id": "b", "wkt": "POINT(2 2)"}]
    db.execute("drop table tg_rdp")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_multi_geometry_columns():
    db.execute("create virtual table tg_rdm using tg0(center point, boundary polygon)")
    db.execute(
        "insert into tg_rdm(center, boundary) values "
        "('POINT(2 2)', 'POLYGON((1 1, 3 1, 3 3, 1 3, 1 1))')"
    )
    db.execute(
        "insert into tg_rdm(center, boundary) values "
        "('POINT(20 20)', 'POLYGON((19 19, 21 19, 21 21, 19 21, 19 19))')"
    )
    q = "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))"
    assert (
        db.execute(
            "select count(*) from tg_rdm where tg_within(center, ?)", [q]
        ).fetchone()[0]
        == 1
    )
    assert (
        db.execute(
            "select count(*) from tg_rdm where tg_within(boundary, ?)", [q]
        ).fetchone()[0]
        == 1
    )
    assert (
        db.execute(
            "select count(*) from tg_rdm where tg_intersects(boundary, 'POINT(20 20)')"
        ).fetchone()[0]
        == 1
    )
    db.execute("drop table tg_rdm")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_eqp(tg0_read_table, snapshot):
    # idxStr surfaces in the VIRTUAL TABLE INDEX detail: header '1' fullscan /
    # '2' point / '3' predicate + [kind][geom-col][op][strategy]
    plans = {
        "fullscan": "select * from tg_rd",
        "point rowid": "select * from tg_rd where rowid = 1",
        "overlap (intersects)": "select * from tg_rd "
        "where tg_intersects(shape, 'POINT(1 1)')",
        "contains (contains)": "select * from tg_rd "
        "where tg_contains(shape, 'POINT(1 1)')",
        "within (within)": "select * from tg_rd "
        "where tg_within(shape, 'POINT(1 1)')",
        "fullscan-exact (disjoint)": "select * from tg_rd "
        "where tg_disjoint(shape, 'POINT(1 1)')",
        "two predicates keep one": "select * from tg_rd "
        "where tg_disjoint(shape, 'POINT(1 1)') and tg_touches(shape, 'POINT(2 2)')",
        "data column predicate not intercepted": "select * from tg_rd "
        "where tg_intersects(label, 'POINT(1 1)')",
    }
    for name, sql in plans.items():
        assert explain_query_plan(sql) == snapshot(name=name)

    db.execute(
        "create virtual table tg_rdp using tg0(id text primary key, shape point)"
    )
    assert explain_query_plan("select * from tg_rdp where id = 'a'") == snapshot(
        name="point text pk"
    )
    db.execute("drop table tg_rdp")

    db.execute("create virtual table tg_rdm using tg0(center point, boundary polygon)")
    assert explain_query_plan(
        "select * from tg_rdm where tg_intersects(center, 'POINT(1 1)')"
    ) == snapshot(name="multi-geometry first column")
    assert explain_query_plan(
        "select * from tg_rdm where tg_intersects(boundary, 'POINT(1 1)')"
    ) == snapshot(name="multi-geometry second column")
    db.execute("drop table tg_rdm")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_float32_ulp_regression():
    import struct

    # The rtree rounds stored bboxes outward to float32. Pick a candidate
    # whose true bbox sits inside the query bbox by less than one float32
    # ulp: both 10.000000001 (query min corner) and 10.000000002 (candidate
    # min corner) round DOWN to 10.0 in float32, so the stored rtree minX
    # (10.0) lands below the exact query minX (10.000000001). Unless the
    # query bbox is also rounded outward before binding, the within-strategy
    # prefilter would false-negative this true match.
    q_min, cand_min = 10.000000001, 10.000000002
    as_f32 = lambda v: struct.unpack("f", struct.pack("f", v))[0]
    assert as_f32(q_min) == 10.0 and as_f32(cand_min) == 10.0
    assert 10.0 < q_min < cand_min

    query = (
        f"POLYGON(({q_min} {q_min}, 20 {q_min}, 20 20, {q_min} 20, {q_min} {q_min}))"
    )
    candidate = (
        f"POLYGON(({cand_min} {cand_min}, 11 {cand_min}, 11 11, "
        f"{cand_min} 11, {cand_min} {cand_min}))"
    )
    db.execute("create virtual table tg_ulp using tg0(shape polygon)")
    db.execute("insert into tg_ulp(shape) values (?)", [candidate])
    # sanity: the exact predicates agree this is a true match
    assert tuple(
        db.execute(
            "select tg_within(?, ?), tg_coveredby(?, ?)",
            [candidate, query, candidate, query],
        ).fetchone()
    ) == (1, 1)
    assert (
        db.execute(
            "select count(*) from tg_ulp where tg_within(shape, ?)", [query]
        ).fetchone()[0]
        == 1
    )
    assert (
        db.execute(
            "select count(*) from tg_ulp where tg_coveredby(shape, ?)", [query]
        ).fetchone()[0]
        == 1
    )
    db.execute("drop table tg_ulp")


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_read_knn_reserved(tg0_read_table, snapshot):
    # the MATCH / distance-constraint surface is reserved for KNN (plan '4')
    with pytest.raises(sqlite3.OperationalError) as excinfo:
        db.execute("select * from tg_rd where shape match 'POINT(1 1)'").fetchall()
    assert str(excinfo.value) == snapshot(name="match")
    with pytest.raises(sqlite3.OperationalError) as excinfo:
        db.execute("select * from tg_rd where distance < 10").fetchall()
    assert str(excinfo.value) == snapshot(name="distance constraint")


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

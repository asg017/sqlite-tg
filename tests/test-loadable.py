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
    "tg_intersects",
    "tg_is_empty",
    "tg_is_feature",
    "tg_is_featurecollection",
    "tg_line",
    "tg_linestring",
    "tg_max_x",
    "tg_max_y",
    "tg_memsize",
    "tg_min_x",
    "tg_min_y",
    "tg_multilinestring",
    "tg_multipoint",
    "tg_multipolygon",
    "tg_point",
    "tg_poly_exterior",
    "tg_polygon",
    "tg_ring",
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


# fmt: off
tg_demo1 = [
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818620800527,32.881627962039275],[-117.23803891594858,32.881627962039275],[-117.23803891594858,32.88150426716983],[-117.23818620800527,32.88150426716983],[-117.23818620800527,32.881627962039275]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23794407791227,32.88163023398593],[-117.23779678585558,32.88163023398593],[-117.23779678585558,32.88150653911649],[-117.23794407791227,32.88150653911649],[-117.23794407791227,32.88163023398593]]]},"properties":{},},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818630582224,32.881454314415976],[-117.23803901376554,32.881454314415976],[-117.23803901376554,32.88133061954653],[-117.23818630582224,32.88133061954653],[-117.23818630582224,32.881454314415976]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.2379432340281,32.881454247902326],[-117.2377959419714,32.881454247902326],[-117.2377959419714,32.88133055303288],[-117.2379432340281,32.88133055303288],[-117.2379432340281,32.881454247902326]]]},"properties":{}},
]
# fmt: on


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0(snapshot):
    db.execute("create virtual table tg_demo1 using tg0();")
    assert execute_all(
        db, "select name from sqlite_master where name like 'tg%' order by 1"
    ) == [
        {"name": "tg_demo1"},
        {"name": "tg_demo1_rtree"},
        {"name": "tg_demo1_rtree_node"},
        {"name": "tg_demo1_rtree_parent"},
        {"name": "tg_demo1_rtree_rowid"},
    ]

    db.execute(
        "insert into tg_demo1(rowid, _shape) select key, value from json_each(?)",
        [json.dumps(tg_demo1)],
    )
    assert db.execute("select count(*) from tg_demo1_rtree").fetchone()[0] == 4

    assert execute_all(
        db, "select rowid, typeof(_shape), tg_to_wkt(_shape) from tg_demo1"
    ) == snapshot(name="tg_demo1 fullscan")

    assert execute_all(
        db, "select id, minX, maxX, minY, maxY from tg_demo1_rtree"
    ) == snapshot(name="tg_demo1 rtree")

    assert re.match(
        "SCAN (TABLE )?tg_demo1 VIRTUAL TABLE INDEX 0:fullscan",
        explain_query_plan("select * from tg_demo1"),
    )

    assert re.match(
        "SCAN (TABLE )?tg_demo1 VIRTUAL TABLE INDEX 150:predicate",
        explain_query_plan("select * from tg_demo1 where tg_intersects(_shape, '')"),
    )
    assert re.match(
        "SCAN (TABLE )?tg_demo1 VIRTUAL TABLE INDEX 152:predicate",
        explain_query_plan("select * from tg_demo1 where tg_contains(_shape, '')"),
    )
    assert re.match(
        "SCAN (TABLE )?tg_demo1 VIRTUAL TABLE INDEX 156:predicate",
        explain_query_plan("select * from tg_demo1 where tg_touches(_shape, '')"),
    )
    assert re.match(
        "SCAN (TABLE )?tg_demo1 VIRTUAL TABLE INDEX 157:predicate",
        explain_query_plan("select * from tg_demo1 where tg_equals(_shape, '')"),
    )
    # TODO rest of predicates?

    def intersecting(feature):
        return [
            row[0]
            for row in db.execute(
                "select rowid from tg_demo1 where tg_intersects(_shape, ?)", [feature]
            ).fetchall()
        ]

    NO_INTERSECT = '{"type":"Feature","properties":{},"geometry":{"type":"LineString","coordinates":[[-117.2382994,32.8816125],[-117.2381318,32.8817785]]}}'
    assert intersecting(NO_INTERSECT) == []

    INTERSECTS_1 = '{"type":"Feature","properties":{},"geometry":{"type":"LineString","coordinates":[[-117.238215,32.8815289],[-117.2380638,32.8816982]]}}'
    assert intersecting(INTERSECTS_1) == [0]
    INTERSECTS_ALL = '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-117.23807203378166,32.881557772296276],[-117.23789358476843,32.881557772296276],[-117.23789358476843,32.88141682228195],[-117.23807203378166,32.88141682228195],[-117.23807203378166,32.881557772296276]]]}}'
    assert intersecting(INTERSECTS_ALL) == [0, 1, 2, 3]
    FULLY_INSIDE_1 = '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-117.2381120521583,32.88156367813649],[-117.23809600069441,32.88156367813649],[-117.23809600069441,32.88154626655529],[-117.2381120521583,32.88154626655529],[-117.2381120521583,32.88156367813649]]]}}'
    assert intersecting(FULLY_INSIDE_1) == [0]
    POINT1 = '{"type":"Feature","properties":{},"geometry":{"type":"Point","coordinates":[-117.2378994,32.8815878]}}'
    assert intersecting(POINT1) == [1]
    RIGHT_2 = '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-117.23782724895766,32.881553840644926],[-117.23764879994442,32.881553840644926],[-117.23764879994442,32.8814128906306],[-117.23782724895766,32.8814128906306],[-117.23782724895766,32.881553840644926]]]}}'
    assert intersecting(RIGHT_2) == [1, 3]

    with pytest.raises(
        sqlite3.OperationalError,
        match="The given predicate inside the WHERE clause on tg0 table is not supported yet",
    ):
        db.execute(
            "select * from tg_demo1 where tg_disjoint(_shape, ?)", [NO_INTERSECT]
        )
    with pytest.raises(
        sqlite3.OperationalError,
        match="The given predicate inside the WHERE clause on tg0 table is not supported yet",
    ):
        db.execute(
            "select * from tg_demo1 where tg_contains(_shape, ?)", [NO_INTERSECT]
        )
    with pytest.raises(
        sqlite3.OperationalError,
        match="The given predicate inside the WHERE clause on tg0 table is not supported yet",
    ):
        db.execute("select * from tg_demo1 where tg_within(_shape, ?)", [NO_INTERSECT])
    with pytest.raises(
        sqlite3.OperationalError,
        match="The given predicate inside the WHERE clause on tg0 table is not supported yet",
    ):
        db.execute("select * from tg_demo1 where tg_covers(_shape, ?)", [NO_INTERSECT])
    with pytest.raises(
        sqlite3.OperationalError,
        match="The given predicate inside the WHERE clause on tg0 table is not supported yet",
    ):
        db.execute(
            "select * from tg_demo1 where tg_coveredby(_shape, ?)", [NO_INTERSECT]
        )

    with pytest.raises(
        sqlite3.OperationalError,
        match="only 1 predicate is allowed on tg0 WHERE clauses.",
    ):
        db.execute(
            "select * from tg_demo1 where tg_intersects(_shape, ?) and tg_intersects(_shape, ?)",
            [NO_INTERSECT, NO_INTERSECT],
        )

    assert db.execute("select count(*) from tg_demo1").fetchone()[0] == 4
    db.execute("delete from tg_demo1 where rowid = 0")
    assert db.execute("select count(*) from tg_demo1").fetchone()[0] == 3

    db.execute("drop table tg_demo1;")
    assert execute_all(db, "select name from sqlite_master where name like 'tg%'") == []


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_touches_and_equals():
    db.execute("create virtual table tg_demo3 using tg0();")
    # rowid 0 and rowid 1 share the edge x=2 (touch, but don't overlap).
    # rowid 3 is an exact duplicate of rowid 0 (equals). rowid 2 is disjoint
    # from everything else.
    db.execute(
        "insert into tg_demo3(rowid, _shape) select key, value from json_each(?)",
        [
            json.dumps(
                [
                    "POLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))",
                    "POLYGON ((2 0, 4 0, 4 2, 2 2, 2 0))",
                    "POLYGON ((10 10, 12 10, 12 12, 10 12, 10 10))",
                    "POLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))",
                ]
            )
        ],
    )

    # both predicates use the rtree index (not a full-scan) when applied
    # directly to the _shape column.
    assert re.match(
        "SCAN (TABLE )?tg_demo3 VIRTUAL TABLE INDEX 156:predicate",
        explain_query_plan("select * from tg_demo3 where tg_touches(_shape, '')"),
    )
    assert re.match(
        "SCAN (TABLE )?tg_demo3 VIRTUAL TABLE INDEX 157:predicate",
        explain_query_plan("select * from tg_demo3 where tg_equals(_shape, '')"),
    )

    def indexed(predicate, feature):
        return sorted(
            row[0]
            for row in db.execute(
                f"select rowid from tg_demo3 where {predicate}(_shape, ?)", [feature]
            ).fetchall()
        )

    def unindexed(predicate, feature):
        # wrapping the column in tg_to_wkt() defeats tg0's xFindFunction
        # interception, so this runs the plain scalar predicate over a
        # full-table scan instead of the rtree-accelerated plan.
        return sorted(
            row[0]
            for row in db.execute(
                f"select rowid from tg_demo3 where {predicate}(tg_to_wkt(_shape), ?)",
                [feature],
            ).fetchall()
        )

    assert re.match(
        "SCAN (TABLE )?tg_demo3 VIRTUAL TABLE INDEX 0:fullscan",
        explain_query_plan(
            "select * from tg_demo3 where tg_touches(tg_to_wkt(_shape), '')"
        ),
    )

    RIGHT_SQUARE = "POLYGON ((2 0, 4 0, 4 2, 2 2, 2 0))"
    LEFT_SQUARE = "POLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))"
    FAR_AWAY = "POLYGON ((20 20, 22 20, 22 22, 20 22, 20 20))"

    assert indexed("tg_touches", RIGHT_SQUARE) == [0, 3]
    assert indexed("tg_touches", RIGHT_SQUARE) == unindexed("tg_touches", RIGHT_SQUARE)
    assert indexed("tg_touches", FAR_AWAY) == []
    assert indexed("tg_touches", FAR_AWAY) == unindexed("tg_touches", FAR_AWAY)

    assert indexed("tg_equals", LEFT_SQUARE) == [0, 3]
    assert indexed("tg_equals", LEFT_SQUARE) == unindexed("tg_equals", LEFT_SQUARE)
    assert indexed("tg_equals", FAR_AWAY) == []
    assert indexed("tg_equals", FAR_AWAY) == unindexed("tg_equals", FAR_AWAY)


@pytest.mark.skipif(not SUPPORTS_RTREE, reason="tg0 require R-Tree extension")
def test_tg0_with_aux():
    db.execute("create virtual table tg_demo2 using tg0(a,b,c);")
    db.execute(
        """
        insert into tg_demo2(rowid, _shape, a, b, c)
        values (1, 'POLYGON ((0 0, 2 2, 4 0, 0 0))', 1, 'text', X'00')
        """
    )
    assert execute_all(
        db, "select rowid, tg_to_wkt(_shape), a, b, c from tg_demo2"
    ) == [
        {
            "rowid": 1,
            "tg_to_wkt(_shape)": "POLYGON((0 0,2 2,4 0,0 0))",
            "a": 1,
            "b": "text",
            "c": b"\0",
        }
    ]


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

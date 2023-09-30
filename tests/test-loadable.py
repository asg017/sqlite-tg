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
    "tg_disjoint",
    "tg_extra_json",
    "tg_geom",
    "tg_geom",
    "tg_group_multipoint",
    "tg_intersects",
    "tg_multipoint",
    "tg_point",
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
]


MODULES = [
    "tg_geometries_each",
    "tg_lines_each",
    "tg_points_each",
    "tg_polygons_each",
    "tg_bbox",
]

SUPPORTS_SUBTYPE = sqlite3.version_info[1] > 38

LINE_A = shapely.from_wkt("LINESTRING (0 0, 1 1)")

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
    assert modules, MODULES


def test_tg_version():
    assert db.execute("select tg_version()").fetchone()[0][0] == "v"


def test_tg_debug():
    debug = db.execute("select tg_debug()").fetchone()[0].split("\n")
    assert len(debug) == 3


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
    # self.skipTest("TODO")


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


@pytest.mark.skip(reason="TODO")
def test_tg_lines_each():
    tg_lines_each = lambda *args: execute_all(
        db, "select rowid, * from tg_lines_each(?)", args
    )
    assert tg_lines_each() == []
    with pytest.raises(sqlite3.OperationalError):
        tg_lines_each()


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
        assert db.execute("select subtype(tg_point(1, 1)").fetchone()[0] == 4

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

    with pytest.raises(sqlite3.OperationalError, match="invalid geometry input. Must be"):
        db.execute("select tg_group_multipoint(NULL)")

    with pytest.raises(sqlite3.OperationalError, match="parameters to tg_group_multipoint\(\) must be Point gemetries"):
        db.execute("select tg_group_multipoint('MULTIPOINT EMPTY')")


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
        match="argument to tg_multipoint\(\) at index 0 is an invalid geometry",
    ):
        tg_multipoint("invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match="argument to tg_multipoint\(\) at index 1 is an invalid geometry",
    ):
        tg_multipoint("point(1 1)", "invalid")

    with pytest.raises(
        sqlite3.OperationalError,
        match="argument to tg_multipoint\(\) at index 0 expected a point, found MultiPoint",
    ):
        tg_multipoint("multipoint(1 1)")


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


if __name__ == "__main__":
    unittest.main()

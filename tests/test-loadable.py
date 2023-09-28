import sqlite3
import json
import unittest
import shapely
from pathlib import Path

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
    "tg_debug",
    "tg_extra_json",
    "tg_geom",
    "tg_geom",
    "tg_intersects",
    "tg_point",
    "tg_point_geojson",
    "tg_point_wkb",
    "tg_point_wkt",
    "tg_to_geojson",
    "tg_to_wkb",
    "tg_to_wkt",
    "tg_type",
    "tg_valid_geojson",
    "tg_valid_wkb",
    "tg_valid_wkt",
    "tg_version",
]

MODULES = [
    "tg_geometries_each",
    "tg_lines_each",
    "tg_points_each",
    "tg_polygons_each",
    "tg_rect_parts",
]


LINE_A = shapely.from_wkt("LINESTRING (0 0, 1 1)")

LINE_CROSSES = (
    shapely.from_wkt("LINESTRING (0 0, 2 2)"),
    shapely.from_wkt("LINESTRING (1 0, 1 2)"),
)
LINE_SEPARATE = (
    shapely.from_wkt("LINESTRING (0 0, 0 2)"),
    shapely.from_wkt("LINESTRING (2 0, 2 2)"),
)

COLLECTION_GEOJSON = (Path(__file__).parent / "data"  / "collection.geojson").read_text()

class TestTg(unittest.TestCase):
    def test_funcs(self):
        funcs = list(
            map(
                lambda a: a[0],
                db.execute("select name from loaded_functions").fetchall(),
            )
        )
        self.assertEqual(funcs, FUNCTIONS)

    def test_modules(self):
        modules = list(
            map(
                lambda a: a[0], db.execute("select name from loaded_modules").fetchall()
            )
        )
        self.assertEqual(modules, MODULES)

    def test_tg_version(self):
        self.assertEqual(db.execute("select tg_version()").fetchone()[0][0], "v")

    def test_tg_debug(self):
        debug = db.execute("select tg_debug()").fetchone()[0].split("\n")
        self.assertEqual(len(debug), 3)

    def test_tg_intersects(self):
        tg_intersects = lambda *args: db.execute(
            "select tg_intersects(?, ?)", args
        ).fetchone()[0]

        for format in ("wkt", "wkb", "geojson"):
            self.assertEqual(
                tg_intersects(
                    *[getattr(shapely, f"to_{format}")(l) for l in LINE_CROSSES]
                ),
                1,
            )
            self.assertEqual(
                tg_intersects(
                    *[getattr(shapely, f"to_{format}")(l) for l in LINE_SEPARATE]
                ),
                0,
            )

    def test_tg_type(self):
        tg_type = lambda *args: db.execute("select tg_type(?)", args).fetchone()[0]
        self.assertEqual(tg_type(shapely.to_wkt(LINE_A)), "LineString")

        self.assertEqual(tg_type("POINT (30 10)"), "Point")
        self.assertEqual(tg_type("LINESTRING (30 10, 10 30, 40 40)"), "LineString")
        self.assertEqual(
            tg_type("POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))"), "Polygon"
        )
        self.assertEqual(
            tg_type("MULTIPOINT (10 40, 40 30, 20 20, 30 10)"), "MultiPoint"
        )
        self.assertEqual(
            tg_type(
                "MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))"
            ),
            "MultiPolygon",
        )
        self.assertEqual(
            tg_type(
                "GEOMETRYCOLLECTION (POINT (40 10),LINESTRING (10 10, 20 20, 10 40),POLYGON ((40 40, 20 45, 45 30, 40 40)))"
            ),
            "GeometryCollection",
        )

        db.execute(
            "select tg_type(tg_point(1, 2)) from json_each('[1,2,3]')"
        ).fetchall()

    def test_tg_to_wkt(self):
        tg_to_wkt = lambda *args: db.execute("select tg_to_wkt(?)", args).fetchone()[0]
        self.assertEqual(tg_to_wkt(shapely.to_wkb(LINE_A)), "LINESTRING(0 0,1 1)")
        # TODO more tests

    def test_tg_to_wkb(self):
        tg_to_wkb = lambda *args: db.execute("select tg_to_wkb(?)", args).fetchone()[0]
        self.assertEqual(
            tg_to_wkb(shapely.to_wkt(LINE_A)),
            b"\x01\x02\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0?\x00\x00\x00\x00\x00\x00\xf0?",
        )
        # TODO more tests

    def test_tg_extra_json(self):
        tg_extra_json = lambda *args: db.execute(
            "select tg_extra_json(?)", args
        ).fetchone()[0]
        self.assertEqual(
            tg_extra_json('{"type": "Point","coordinates": [-118.2097812,34.0437074]}'),
            None,
        )
        self.assertEqual(
            tg_extra_json(
                '{"id": "A", "type": "Point","coordinates": [-118.2097812,34.0437074]}'
            ),
            '{"id":"A"}',
        )

    def test_tg_geom(self):
        self.assertEqual(
            db.execute("select tg_geom(?)", ["POINT(1 1)"]).fetchone()[0], None
        )
        self.assertEqual(
            db.execute("select subtype(tg_geom(?))", ["POINT(1 1)"]).fetchone()[0], 112
        )
        with self.assertRaisesRegex(
            sqlite3.OperationalError, "ParseError: invalid text"
        ):
            db.execute("select tg_geom(?)", ["POINT(1 1"]).fetchone()
        # self.skipTest("TODO")

    def test_tg_valid_geojson(self):
        tg_valid_geojson = lambda *args: db.execute(
            "select tg_valid_geojson(?)", args
        ).fetchone()[0]
        self.assertEqual(tg_valid_geojson(shapely.to_geojson(LINE_A)), 1)
        self.assertEqual(tg_valid_geojson(shapely.to_wkt(LINE_A)), 0)

    def test_tg_valid_wkb(self):
        tg_valid_wkb = lambda *args: db.execute(
            "select tg_valid_wkb(?)", args
        ).fetchone()[0]
        self.assertEqual(tg_valid_wkb(shapely.to_wkb(LINE_A)), 1)
        self.assertEqual(tg_valid_wkb(shapely.to_wkt(LINE_A)), 0)

    def test_tg_valid_wkt(self):
        tg_valid_wkt = lambda *args: db.execute(
            "select tg_valid_wkt(?)", args
        ).fetchone()[0]
        self.assertEqual(tg_valid_wkt(shapely.to_wkt(LINE_A)), 1)
        self.assertEqual(tg_valid_wkt(shapely.to_wkb(LINE_A)), 0)

    def test_tg_geometries_each(self):
        tg_geometries_each = lambda *args: execute_all(
            db, "select rowid, * from tg_geometries_each(?)", args
        )
        self.assertEqual(tg_geometries_each(COLLECTION_GEOJSON), [])
        with self.assertRaisesRegex(sqlite3.OperationalError, "XXX"):
            tg_geometries_each(1)

    def test_tg_lines_each(self):
        self.skipTest("TODO")
        tg_lines_each = lambda *args: execute_all(
            db, "select rowid, * from tg_lines_each(?)", args
        )
        self.assertEqual(tg_lines_each(), [])
        with self.assertRaisesRegex(sqlite3.OperationalError):
            tg_lines_each()

    def test_tg_points_each(self):
        self.skipTest("TODO")
        tg_points_each = lambda *args: execute_all(
            db, "select rowid, * from tg_points_each(?)", args
        )
        self.assertEqual(tg_points_each(), [])
        with self.assertRaisesRegex(sqlite3.OperationalError):
            tg_points_each()

    def test_tg_polygons_each(self):
        self.skipTest("TODO")
        tg_polygons_each = lambda *args: execute_all(
            db, "select rowid, * from tg_polygons_each(?)", args
        )
        self.assertEqual(tg_polygons_each(), [])
        with self.assertRaisesRegex(sqlite3.OperationalError):
            tg_polygons_each()

    def test_tg_rect_parts(self):
        self.skipTest("TODO")
        tg_rect_parts = lambda *args: execute_all(
            db, "select rowid, * from tg_rect_parts(?)", args
        )
        self.assertEqual(tg_rect_parts(), [])
        with self.assertRaisesRegex(sqlite3.OperationalError):
            tg_rect_parts()

    def test_tg_to_geojson(self):
        tg_to_geojson = lambda *args: db.execute(
            "select tg_to_geojson(?)", args
        ).fetchone()[0]
        self.assertEqual(
            tg_to_geojson(shapely.to_wkt(LINE_A)),
            '{"type":"LineString","coordinates":[[0,0],[1,1]]}',
        )
        # TODO more tests

    def test_tg_point(self):
        self.assertEqual(db.execute("select tg_point(1, 1)").fetchone()[0], None)
        self.assertEqual(
            db.execute("select tg_to_wkt(tg_point(1, 1))").fetchone()[0], "POINT(1 1)"
        )
        self.assertEqual(
            [
                row[0]
                for row in db.execute(
                    "select tg_to_wkt(tg_point(1, 1)) from json_each('[1,2]')"
                ).fetchall()
            ],
            ["POINT(1 1)", "POINT(1 1)"],
        )

        if sqlite3.version_info[1] > 38:
            self.assertEqual(
                db.execute("select subtype(tg_point(1, 1)").fetchone()[0], 4
            )
        else:
            self.skipTest("not on SQLite version 3.38 or above")

    def test_tg_point_geojson(self):
        tg_point_geojson = lambda *args: db.execute(
            "select tg_point_geojson(?, ?)", args
        ).fetchone()[0]
        self.assertEqual(tg_point_geojson(1, 2), '{"type":"Point","coordinates":[1,2]}')
        self.assertEqual(
            tg_point_geojson(1.111, 2.222),
            '{"type":"Point","coordinates":[1.111,2.222]}',
        )

    def test_tg_point_wkb(self):
        tg_point_wkb = lambda *args: db.execute(
            "select tg_point_wkb(?, ?)", args
        ).fetchone()[0]
        self.assertEqual(
            tg_point_wkb(1, 2),
            b"\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0?\x00\x00\x00\x00\x00\x00\x00@",
        )
        self.assertEqual(
            tg_point_wkb(1.111, 2.222),
            b"\x01\x01\x00\x00\x00-\xb2\x9d\xef\xa7\xc6\xf1?-\xb2\x9d\xef\xa7\xc6\x01@",
        )

    def test_tg_point_wkt(self):
        tg_point_wkt = lambda *args: db.execute(
            "select tg_point_wkt(?, ?)", args
        ).fetchone()[0]
        self.assertEqual(tg_point_wkt(1, 2), "POINT(1 2)")
        self.assertEqual(tg_point_wkt(1.111, 2.222), "POINT(1.111 2.222)")

    def test_tg_points_each(self):
        self.assertEqual(
            execute_all(
                db,
                "select rowid, point, subtype(point), tg_to_wkt(point), tg_to_geojson(point) from tg_points_each(?)",
                ["MULTIPOINT ((10 40), (40 30), (20 20), (30 10))"],
            ),
            [
                {
                    "rowid": 0,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(10 40)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[10,40]}',
                },
                {
                    "rowid": 1,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(40 30)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[40,30]}',
                },
                {
                    "rowid": 2,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(20 20)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[20,20]}',
                },
                {
                    "rowid": 3,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(30 10)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[30,10]}',
                },
            ],
        )
        self.maxDiff = 10000
        self.assertEqual(
            execute_all(
                db,
                """
                  select
                    tg_points_each.rowid,
                    point,
                    subtype(point),
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
            ),
            [
                {
                    "rowid": 0,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(10 20)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[10,20]}',
                },
                {
                    "rowid": 1,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(30 40)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[30,40]}',
                },
                {
                    "rowid": 0,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(90 80)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[90,80]}',
                },
                {
                    "rowid": 1,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(70 60)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[70,60]}',
                },
                {
                    "rowid": 2,
                    "point": None,
                    "subtype(point)": 112,
                    "tg_to_wkt(point)": "POINT(50 40)",
                    "tg_to_geojson(point)": '{"type":"Point","coordinates":[50,40]}',
                },
            ],
        )


class TestCoverage(unittest.TestCase):
    def test_coverage(self):
        test_methods = [
            method for method in dir(TestTg) if method.startswith("test_tg")
        ]
        funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
        for func in [*FUNCTIONS, *MODULES]:
            self.assertTrue(
                func in funcs_with_tests,
                f"{func} does not have cooresponding test in {funcs_with_tests}",
            )


class TestDocs(unittest.TestCase):
    def test_docs(self):
        README = (Path(__file__).parent.parent / "docs.md").read_text()
        for func in [*FUNCTIONS, *MODULES]:
            self.assertTrue(func in README, f"{func} is not documented")
            self.assertTrue(f'name="{func}"' in README, f"{func} is not documented")
            self.assertTrue(f"{func}(" in README, f"{func} missing code sample")


if __name__ == "__main__":
    unittest.main()

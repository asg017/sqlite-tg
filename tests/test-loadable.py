import sqlite3
import unittest
import shapely

EXT_PATH="./dist/tg0"


def connect(ext, path=":memory:"):
  db = sqlite3.connect(path)

  db.execute("create temp table base_functions as select name from pragma_function_list")
  db.execute("create temp table base_modules as select name from pragma_module_list")

  db.enable_load_extension(True)
  db.load_extension(ext)

  db.execute("create temp table loaded_functions as select name from pragma_function_list where name not in (select name from base_functions) order by name")
  db.execute("create temp table loaded_modules as select name from pragma_module_list where name not in (select name from base_modules) order by name")

  db.row_factory = sqlite3.Row
  return db


db = connect(EXT_PATH)

def explain_query_plan(sql):
  return db.execute("explain query plan " + sql).fetchone()["detail"]

def execute_all(cursor, sql, args=None):
  if args is None: args = []
  results = cursor.execute(sql, args).fetchall()
  return list(map(lambda x: dict(x), results))

def spread_args(args):
  return ",".join(['?'] * len(args))

FUNCTIONS = [
  'tg_debug',
  'tg_intersects',
  'tg_point_geojson',
  'tg_point_wkb',
  'tg_point_wkt',
  'tg_type',
  'tg_version',
]

MODULES = []


LINE_A = shapely.from_wkt('LINESTRING (0 0, 1 1)')

LINE_CROSSES = (shapely.from_wkt('LINESTRING (0 0, 2 2)'), shapely.from_wkt('LINESTRING (1 0, 1 2)'))
LINE_SEPARATE = (shapely.from_wkt('LINESTRING (0 0, 0 2)'), shapely.from_wkt('LINESTRING (2 0, 2 2)'))

class TestTg(unittest.TestCase):
  def test_funcs(self):
    funcs = list(map(lambda a: a[0], db.execute("select name from loaded_functions").fetchall()))
    self.assertEqual(funcs, FUNCTIONS)

  def test_modules(self):
    modules = list(map(lambda a: a[0], db.execute("select name from loaded_modules").fetchall()))
    self.assertEqual(modules, MODULES)


  def test_tg_version(self):
    self.assertEqual(db.execute("select tg_version()").fetchone()[0][0], "v")

  def test_tg_debug(self):
    debug = db.execute("select tg_debug()").fetchone()[0].split('\n')
    self.assertEqual(len(debug), 3)

  def test_tg_intersects(self):
    tg_intersects = lambda *args: db.execute("select tg_intersects(?, ?)", args).fetchone()[0]

    for format in ("wkt", "wkb", "geojson"):
      self.assertEqual(tg_intersects(*[getattr(shapely, f"to_{format}")(l) for l in LINE_CROSSES]), 1)
      self.assertEqual(tg_intersects(*[getattr(shapely, f"to_{format}")(l) for l in LINE_SEPARATE]), 0)

  def test_tg_type(self):
    tg_type = lambda *args: db.execute("select tg_type(?)", args).fetchone()[0]
    self.assertEqual(tg_type(shapely.to_wkt(LINE_A)), 'LineString')

  def test_tg_point_geojson(self):
    tg_point_geojson = lambda *args: db.execute("select tg_point_geojson(?, ?)", args).fetchone()[0]
    self.assertEqual(tg_point_geojson(1, 2), '{"type":"Point","coordinates":[1,2]}')
    self.assertEqual(tg_point_geojson(1.111, 2.222), '{"type":"Point","coordinates":[1.111,2.222]}')

  def test_tg_point_wkb(self):
    tg_point_wkb = lambda *args: db.execute("select tg_point_wkb(?, ?)", args).fetchone()[0]
    self.assertEqual(tg_point_wkb(1, 2), b'\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0?\x00\x00\x00\x00\x00\x00\x00@')
    self.assertEqual(tg_point_wkb(1.111, 2.222), b'\x01\x01\x00\x00\x00-\xb2\x9d\xef\xa7\xc6\xf1?-\xb2\x9d\xef\xa7\xc6\x01@')

  def test_tg_point_wkt(self):
    tg_point_wkt = lambda *args: db.execute("select tg_point_wkt(?, ?)", args).fetchone()[0]
    self.assertEqual(tg_point_wkt(1, 2), 'POINT(1 2)')
    self.assertEqual(tg_point_wkt(1.111, 2.222), 'POINT(1.111 2.222)')


class TestCoverage(unittest.TestCase):
  def test_coverage(self):
    test_methods = [method for method in dir(TestTg) if method.startswith('test_tg')]
    funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
    for func in FUNCTIONS:
      self.assertTrue(func in funcs_with_tests, f"{func} does not have cooresponding test in {funcs_with_tests}")

if __name__ == '__main__':
    unittest.main()

import sqlite3
import unittest
import time
import os

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
  'tg_version',
]

MODULES = []

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
    self.assertEqual(tg_intersects('LINESTRING (0 0, 2 2)', 'LINESTRING (1 0, 1 2)'), 1)
    self.assertEqual(tg_intersects('LINESTRING (0 0, 0 2)', 'LINESTRING (2 0, 2 2)'), 0)


class TestCoverage(unittest.TestCase):
  def test_coverage(self):
    test_methods = [method for method in dir(TestTg) if method.startswith('test_tg')]
    funcs_with_tests = set([x.replace("test_", "") for x in test_methods])
    for func in FUNCTIONS:
      self.assertTrue(func in funcs_with_tests, f"{func} does not have cooresponding test in {funcs_with_tests}")

if __name__ == '__main__':
    unittest.main()

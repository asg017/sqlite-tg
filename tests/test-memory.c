#include "sqlite3.h"
#include "sqlite-tg.h"
#include <stdio.h>

// Exercises the extension against the vendored SQLite, failing loudly on any
// error. Run under a leak checker (`leaks -atExit --`, valgrind) to verify
// geometry lifetimes; the statements below cover every ownership pattern:
// scalar constructors, pointer round-trips, upcast rings, each-vtabs,
// aggregates, error paths, and the tg0 rtree table.

static int exec_ok(sqlite3 *db, const char *sql) {
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "❌ [%s] failed: %s\n", sql, err ? err : sqlite3_errmsg(db));
    sqlite3_free(err);
    return 1;
  }
  return 0;
}

// statements that must error, without crashing or leaking
static int exec_expect_error(sqlite3 *db, const char *sql) {
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (rc == SQLITE_OK) {
    fprintf(stderr, "❌ [%s] unexpectedly succeeded\n", sql);
    return 1;
  }
  sqlite3_free(err);
  return 0;
}

int main(int argc, char *argv[]) {
  sqlite3 *db = NULL;
  int failures = 0;

  int rc = sqlite3_auto_extension((void (*)())sqlite3_tg_init);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "❌ could not register sqlite3_tg_init: %d\n", rc);
    return 1;
  }

  rc = sqlite3_open(":memory:", &db);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "❌ cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  static const char *OK_STATEMENTS[] = {
      "select tg_version()",
      "select tg_to_wkt(tg_point(1, 2))",
      "select tg_to_wkb(tg_point(1, 2))",
      "select tg_to_geojson(tg_point(1, 2))",
      "select tg_point(1, 2)",
      "select tg_type('POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))')",
      "select tg_extra_json('{\"id\": 1, \"type\": \"Point\", \"coordinates\": [1, 2]}')",
      "select tg_to_wkt(tg_line(tg_point(0, 0), tg_point(1, 1)))",
      "select tg_to_wkt(tg_multipoint(tg_point(0, 0), tg_point(1, 1)))",
      "select tg_to_wkt(tg_poly_exterior("
      "'POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10), (20 30, 35 35, 30 20, 20 30))'))",
      "select tg_intersects('LINESTRING (0 0, 2 2)', 'LINESTRING (1 0, 1 2)')",
      "select tg_valid_wkt('POINT(1 1)'), tg_valid_wkt('nope')",
      "select tg_to_wkt(point) from tg_points_each('MULTIPOINT (10 40, 40 30)')",
      "select tg_to_wkt(geometry) from tg_geometries_each("
      "'GEOMETRYCOLLECTION (POINT (40 10), LINESTRING (10 10, 20 20, 10 40))')",
      "select tg_to_wkt(hole) from tg_holes_each("
      "'POLYGON ((0 0, 10 0, 10 10, 0 0), (1 1, 2 1, 2 2, 1 1))')",
      "select count(*) from tg_holes_each('POINT(1 1)')",
      "select * from tg_bbox('LINESTRING (30 10, 10 30, 40 40)')",
      "select tg_to_wkt(tg_group_multipoint(tg_point(value, value))) "
      "from json_each('[1, 2, 3]')",
      "select tg_to_wkt(tg_group_geometry_collection(value)) "
      "from json_each('[\"POINT(1 1)\", \"LINESTRING(0 0, 1 1)\"]')",
      "select tg_group_feature_collection_geojson(value) "
      "from json_each('[\"POINT(1 1)\", \"POINT(2 2)\"]')",
      "select tg_to_wkt(tg_group_bbox(value)) "
      "from json_each('[\"POINT(1 1)\", \"POINT(5 9)\"]')",
      "create virtual table temp.demo using tg0(label)",
      "insert into temp.demo(rowid, _shape, label) "
      "values (1, tg_geom('POINT(1 1)'), 'a'), (2, tg_geom('POINT(9 9)'), 'b')",
      "select rowid, label from temp.demo "
      "where tg_intersects(_shape, 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))')",
      "delete from temp.demo where rowid = 1",
      "drop table temp.demo",
  };
  static const char *ERROR_STATEMENTS[] = {
      "select tg_to_wkt('not a geometry')",
      "select tg_point('a', 1)",
      "select tg_geom('POINT(0 1)', 'not-an-index')",
      "select tg_poly_exterior('POINT(1 1)')",
      "select tg_multipoint('LINESTRING(0 0, 1 1)')",
      "select tg_intersects('POINT(1 1)', 'nope')",
      "select tg_to_wkt(tg_group_multipoint(value)) "
      "from json_each('[\"LINESTRING(0 0, 1 1)\"]')",
  };

  for (int i = 0; i < sizeof(OK_STATEMENTS) / sizeof(OK_STATEMENTS[0]); i++) {
    failures += exec_ok(db, OK_STATEMENTS[i]);
  }
  for (int i = 0; i < sizeof(ERROR_STATEMENTS) / sizeof(ERROR_STATEMENTS[0]);
       i++) {
    failures += exec_expect_error(db, ERROR_STATEMENTS[i]);
  }

  sqlite3_close(db);
  if (failures) {
    fprintf(stderr, "❌ %d statement(s) failed\n", failures);
    return 1;
  }
  fprintf(stderr, "✅ test-memory: all statements behaved as expected\n");
  return 0;
}

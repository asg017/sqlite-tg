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
      "select tg_to_wkt(tg_linestring(tg_point(0, 0), tg_point(1, 1)))",
      "select tg_to_wkt(tg_multipoint(tg_point(0, 0), tg_point(1, 1)))",
      "select tg_to_wkt(tg_poly_exterior("
      "'POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10), (20 30, 35 35, 30 20, 20 30))'))",
      "select tg_to_wkt(tg_ring())",
      "select tg_to_wkt(tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10)))",
      "select tg_to_wkt(tg_polygon())",
      "select tg_to_wkt(tg_polygon(tg_ring(tg_point(0, 0), tg_point(10, 0), "
      "tg_point(10, 10), tg_point(0, 10), tg_point(0, 0))))",
      "select tg_to_wkt(tg_polygon("
      "tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 10), tg_point(0, 0)), "
      "tg_ring(tg_point(1, 1), tg_point(2, 1), tg_point(2, 2), tg_point(1, 2), tg_point(1, 1))))",
      "select tg_to_wkt(tg_polygon('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))'))",
      "select tg_to_wkt(tg_multilinestring())",
      "select tg_to_wkt(tg_multilinestring("
      "tg_line(tg_point(0, 0), tg_point(1, 1)), tg_line(tg_point(2, 2), tg_point(3, 3))))",
      "select tg_to_wkt(tg_multipolygon())",
      "select tg_to_wkt(tg_multipolygon("
      "'POLYGON((0 0, 1 0, 1 1, 0 0))', 'POLYGON((10 10, 11 10, 11 11, 10 10))'))",
      "select tg_to_wkt(tg_multipolygon("
      "tg_ring(tg_point(0, 0), tg_point(1, 0), tg_point(1, 1), tg_point(0, 0))))",
      "select tg_to_wkt(tg_geometrycollection())",
      "select tg_to_wkt(tg_geometrycollection('POINT(1 1)', 'LINESTRING(0 0, 1 1)'))",
      "select tg_to_wkt(tg_geometrycollection("
      "'POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))', "
      "'GEOMETRYCOLLECTION(POINT(2 2))'))",
      "select tg_intersects('LINESTRING (0 0, 2 2)', 'LINESTRING (1 0, 1 2)')",
      "select tg_equals('POINT(1 1)', 'POINT(1 1)')",
      "select tg_valid_wkt('POINT(1 1)'), tg_valid_wkt('nope')",
      "select tg_to_wkt(point) from tg_points_each('MULTIPOINT (10 40, 40 30)')",
      "select tg_to_wkt(geometry) from tg_geometries_each("
      "'GEOMETRYCOLLECTION (POINT (40 10), LINESTRING (10 10, 20 20, 10 40))')",
      "select tg_to_wkt(hole) from tg_holes_each("
      "'POLYGON ((0 0, 10 0, 10 10, 0 0), (1 1, 2 1, 2 2, 1 1))')",
      "select count(*) from tg_holes_each('POINT(1 1)')",
      "select tg_to_wkt(point) from tg_line_points_each("
      "'LINESTRING (10 40, 40 30, 20 20)')",
      "select ax, ay, bx, by from tg_line_segments_each("
      "'LINESTRING (10 40, 40 30, 20 20)')",
      "select count(*) from tg_line_points_each('LINESTRING EMPTY')",
      "select tg_to_wkt(point) from tg_ring_points_each("
      "tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 0))'))",
      "select ax, ay, bx, by from tg_ring_segments_each("
      "tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 0))'))",
      "select tg_to_wkt(point) from tg_ring_points_each("
      "'POLYGON ((0 0, 10 0, 10 10, 0 0))')",
      "select ax, ay, bx, by from tg_ring_segments_each("
      "'POLYGON ((0 0, 10 0, 10 10, 0 0))')",
      "select tg_dims(tg_point(1, 2))",
      "select tg_is_empty('POINT EMPTY')",
      "select tg_is_feature('{\"type\": \"Feature\", \"geometry\": "
      "{\"type\": \"Point\", \"coordinates\": [1, 2]}, \"properties\": {}}')",
      "select tg_is_featurecollection('{\"type\": \"FeatureCollection\", \"features\": []}')",
      "select tg_memsize(tg_point(1, 2))",
      "select tg_x(tg_point(1, 2))",
      "select tg_y(tg_point(1, 2))",
      "select tg_min_x('POLYGON ((1 1, 2 2, 3 3, 1 1))')",
      "select tg_min_y('POLYGON ((1 1, 2 2, 3 3, 1 1))')",
      "select tg_max_x('POLYGON ((1 1, 2 2, 3 3, 1 1))')",
      "select tg_max_y('POLYGON ((1 1, 2 2, 3 3, 1 1))')",
      "select * from tg_bbox('LINESTRING (30 10, 10 30, 40 40)')",
      "select tg_to_wkt(tg_group_multipoint(tg_point(value, value))) "
      "from json_each('[1, 2, 3]')",
      "select tg_to_wkt(tg_group_line(tg_point(value ->> 0, value ->> 1))) "
      "from json_each('[[1, 1], [2, 2]]')",
      "select tg_to_wkt(tg_group_line(tg_point(1, 1))) from (select 1 where 0)",
      "select tg_to_wkt(tg_group_ring(tg_point(value ->> 0, value ->> 1))) "
      "from json_each('[[0, 0], [10, 0], [10, 10]]')",
      "select tg_to_wkt(tg_group_ring(tg_point(1, 1))) from (select 1 where 0)",
      "select tg_to_wkt(tg_group_polygon(value)) from json_each("
      "'[\"POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))\", "
      "\"POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))\"]')",
      "select tg_to_wkt(tg_group_polygon(tg_ring(tg_point(1, 1)))) "
      "from (select 1 where 0)",
      "select tg_to_wkt(tg_group_geometrycollection(value)) "
      "from json_each('[\"POINT(1 1)\", \"LINESTRING(0 0, 1 1)\"]')",
      // deprecated alias must still work, same step/final pair
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
      "select rowid, label from temp.demo "
      "where tg_touches(_shape, 'LINESTRING(1 1, 5 5)')",
      "select rowid, label from temp.demo "
      "where tg_equals(_shape, 'POINT(1 1)')",
      "delete from temp.demo where rowid = 1",
      "drop table temp.demo",
  };
  static const char *ERROR_STATEMENTS[] = {
      "select tg_to_wkt('not a geometry')",
      "select tg_point('a', 1)",
      "select tg_geom('POINT(0 1)', 'not-an-index')",
      "select tg_poly_exterior('POINT(1 1)')",
      "select tg_multipoint('LINESTRING(0 0, 1 1)')",
      "select tg_ring(tg_point(0, 0), 'not point', tg_point(1, 1))",
      "select tg_ring('not a geometry')",
      "select tg_polygon(tg_ring(tg_point(0, 0), tg_point(1, 0), tg_point(1, 1), "
      "tg_point(0, 0)), tg_ring(tg_point(0, 0), tg_point(1, 0), tg_point(1, 1), "
      "tg_point(0, 0)), 'not a ring')",
      "select tg_polygon('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), "
      "(1 1, 2 1, 2 2, 1 2, 1 1))')",
      "select tg_multilinestring(tg_line(tg_point(0, 0), tg_point(1, 1)), "
      "tg_line(tg_point(2, 2), tg_point(3, 3)), 'POINT(1 1)')",
      "select tg_multipolygon('POLYGON((0 0, 1 0, 1 1, 0 0))', "
      "'POLYGON((10 10, 11 10, 11 11, 10 10))', 'not a polygon')",
      "select tg_geometrycollection('POINT(1 1)', 'LINESTRING(0 0, 1 1)', "
      "'not a geometry')",
      "select tg_dims(NULL)",
      "select tg_is_empty('not a geometry')",
      "select tg_is_feature(NULL)",
      "select tg_is_featurecollection('not a geometry')",
      "select tg_memsize(NULL)",
      "select tg_x('LINESTRING(0 0, 1 1)')",
      "select tg_y(NULL)",
      "select tg_min_x(NULL)",
      "select tg_intersects('POINT(1 1)', 'nope')",
      "select tg_equals('POINT(1 1)', 'nope')",
      "select tg_to_wkt(tg_group_multipoint(value)) "
      "from json_each('[\"LINESTRING(0 0, 1 1)\"]')",
      // error mid-aggregation: a good row is stepped (growing the aggregate
      // context's point array) before the error-triggering row is hit
      "select tg_to_wkt(tg_group_line(value)) from json_each("
      "'[\"POINT(1 1)\", \"LINESTRING(0 0, 1 1)\"]')",
      "select tg_to_wkt(tg_group_ring(value)) from json_each("
      "'[\"POINT(1 1)\", \"LINESTRING(0 0, 1 1)\"]')",
      "select tg_to_wkt(tg_group_polygon(value)) from json_each("
      "'[\"POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))\", \"POINT(1 1)\"]')",
      "select tg_group_polygon(value) from json_each("
      "'[\"POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))\"]')",
      "select * from tg_line_points_each('POINT(1 1)')",
      "select * from tg_line_segments_each('POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))')",
      "select * from tg_ring_points_each('POINT(1 1)')",
      "select * from tg_ring_segments_each('MULTIPOINT (1 1, 2 2)')",
      "select * from tg_line_points_each(NULL)",
      "select * from tg_ring_points_each(NULL)",
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

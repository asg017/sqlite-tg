#include "sqlite3.h"
#include "sqlite-tg.h"
#include <stdio.h>
#include <unistd.h>


int main(int argc, char *argv[]) {
  int rc = SQLITE_OK;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char* error_message;

  rc = sqlite3_auto_extension((void (*)())sqlite3_tg_init);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "❌ demo.c could not load sqlite3_tg_init: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  // this database connection will now have all sqlite-vss SQL functions available
  rc = sqlite3_open(":memory:", &db);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "❌ demo.c cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  rc = sqlite3_exec(db, (
      "select tg_to_wkt(tg_group_geometrycollection(tg_to_wkt(geometry)))"
      "from tg_geometries_each('GEOMETRYCOLLECTION (POINT (40 10),"
      "LINESTRING (10 10, 20 20, 10 40),"
      "POLYGON ((40 40, 20 45, 45 30, 40 40)))')"

  ), NULL, NULL, NULL);
  sqlite3_close(db);
  return 0;
}

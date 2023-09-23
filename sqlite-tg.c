#include "sqlite3ext.h"

SQLITE_EXTENSION_INIT1

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tg.h>


static void tgVersionFunc(sqlite3_context *context, int argc,
                             sqlite3_value **argv) {
  sqlite3_result_text(context, sqlite3_user_data(context), -1, SQLITE_STATIC);
}

// TODO is this deterministic?
static void tgDebugFunc(sqlite3_context *context, int argc,
                           sqlite3_value **arg) {
  sqlite3_result_text(context, sqlite3_user_data(context), -1, SQLITE_STATIC);
}
static void tgIntersectsFunc(sqlite3_context *context, int argc,
                           sqlite3_value **argv) {
  struct tg_geom *a = tg_parse_wkt(sqlite3_value_text(argv[0]));
    if (tg_geom_error(a)) {
      sqlite3_result_error(context,tg_geom_error(a),  -1);
      return;
    }
    struct tg_geom *b = tg_parse_wkt(sqlite3_value_text(argv[1]));
    if (tg_geom_error(b)) {
        tg_geom_free(a);
        sqlite3_result_error(context,tg_geom_error(b),  -1);
        return;
    }
    sqlite3_result_int(context, tg_geom_intersects(a, b));

    tg_geom_free(a);
    tg_geom_free(b);
}

#pragma endregion

#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_tg_init(sqlite3 *db, char **pzErrMsg,
                           const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  (void)pzErrMsg; /* Unused parameter */
  int flags = SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC;

  const char *debug = sqlite3_mprintf(
      "Version: %s\nDate: %s\nSource: %s", SQLITE_TG_VERSION,
      SQLITE_TG_DATE, SQLITE_TG_SOURCE);



  if (rc == SQLITE_OK)
    rc = sqlite3_create_function_v2(db, "tg_version", 0, flags,
                                    (void *)SQLITE_TG_VERSION,
                                    tgVersionFunc, 0, 0, 0);
  if (rc == SQLITE_OK)
    rc = sqlite3_create_function_v2(db, "tg_debug", 0, flags, (void *)debug,tgDebugFunc, 0, 0, sqlite3_free);
    rc = sqlite3_create_function_v2(db, "tg_intersects", 2, flags, 0,tgIntersectsFunc, 0, 0, 0);
  return rc;
}

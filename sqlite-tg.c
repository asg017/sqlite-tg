#include "sqlite3ext.h"

SQLITE_EXTENSION_INIT1

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tg.h>

// https://github.com/sqlite/sqlite/blob/2d3c5385bf168c85875c010bbaa79c6712eab214/src/json.c#L125-L126
#define JSON_SUBTYPE 74

#pragma region value

static const char *TG_GEOM_POINTER_NAME = "tg0-tg_geom";

static struct tg_geom *geomValue(sqlite3_value *value) {
  int n = sqlite3_value_bytes(value);
  switch (sqlite3_value_type(value)) {
  case SQLITE_BLOB: {
    return tg_parse_wkb_ix(sqlite3_value_blob(value), n, TG_NONE);
  }
  case SQLITE_TEXT: {
    const char *text = (const char *)sqlite3_value_text(value);
    if (sqlite3_value_subtype(value) == JSON_SUBTYPE ||
        // JSON is many SQL queries don't have a subtype,
        // so to distinguish between WKT/geojson, see if it
        // starts with "{", which almost always is geojson
        (n > 0 && text[0] == '{')) {
      return tg_parse_geojsonn_ix(text, n, TG_NONE);
    } else {
      return tg_parse_wktn_ix(text, n, TG_NONE);
    }
  }
  case SQLITE_NULL: {
    void *p = sqlite3_value_pointer(value, TG_GEOM_POINTER_NAME);
    if (p) {
      return (struct tg_geom *)p;
    }
  }
  }
  return NULL;
}

#pragma endregion

#pragma resulting

static void resultGeomWkt(sqlite3_context *context, struct tg_geom *geom) {
  size_t size = tg_geom_wkt(geom, 0, 0);
  void *buffer = sqlite3_malloc(size + 1);
  if (buffer == 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  // TODO assert enough space?
  tg_geom_wkt(geom, buffer, size + 1);
  sqlite3_result_text(context, buffer, size, sqlite3_free);
}
static void resultGeomWkb(sqlite3_context *context, struct tg_geom *geom) {
  size_t size = tg_geom_wkb(geom, 0, 0);
  void *buffer = sqlite3_malloc(size + 1);
  if (buffer == 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  // TODO assert enough space?
  tg_geom_wkb(geom, buffer, size + 1);
  sqlite3_result_blob(context, buffer, size, sqlite3_free);
}
static void resultGeomGeojson(sqlite3_context *context, struct tg_geom *geom) {
  size_t size = tg_geom_geojson(geom, 0, 0);
  void *buffer = sqlite3_malloc(size + 1);
  if (buffer == 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  // TODO assert enough space?
  tg_geom_geojson(geom, buffer, size + 1);
  sqlite3_result_text(context, buffer, size, sqlite3_free);
  sqlite3_result_subtype(context, JSON_SUBTYPE);
}

static void resultGeomPointer(sqlite3_context *context, struct tg_geom *geom) {
  sqlite3_result_pointer(context, geom, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}

enum export_format { WKT = 1, WKB = 2, GEOJSON = 3, POINTER = 4 };

static void resultAndFreeGeomFormat(sqlite3_context *context,
                                    struct tg_geom *geom,
                                    enum export_format format) {
  switch (format) {
  case WKT: {
    resultGeomWkt(context, geom);
    tg_geom_free(geom);
    break;
  }
  case WKB: {
    resultGeomWkb(context, geom);
    tg_geom_free(geom);
    break;
  }
  case GEOJSON: {
    resultGeomGeojson(context, geom);
    tg_geom_free(geom);
    break;
  }
  case POINTER: {
    // takes ownership of geom, no need to free
    resultGeomPointer(context, geom);
    break;
  }
  }
}

#pragma endregion

#pragma region scalar functions
static void tg_version(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  sqlite3_result_text(context, SQLITE_TG_VERSION, -1, SQLITE_STATIC);
}

static void tg_debug(sqlite3_context *context, int argc, sqlite3_value **arg) {
  sqlite3_result_text(context, sqlite3_user_data(context), -1, SQLITE_STATIC);
}
static void tg_to_wkt(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  struct tg_geom *geom = geomValue(argv[0]);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomWkt(context, geom);
  tg_geom_free(geom);
}

static void tg_to_wkb(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  struct tg_geom *geom = geomValue(argv[0]);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomWkb(context, geom);
  tg_geom_free(geom);
}

static void tg_to_geojson(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  struct tg_geom *geom = geomValue(argv[0]);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomGeojson(context, geom);
  tg_geom_free(geom);
}

static void tg_intersects(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  struct tg_geom *a = geomValue(argv[0]);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  struct tg_geom *b = geomValue(argv[1]);
  if (tg_geom_error(b)) {
    tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_intersects(a, b));

  tg_geom_free(a);
  tg_geom_free(b);
}
static void tg_type(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom = geomValue(argv[0]);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_text(context, tg_geom_type_string(tg_geom_typeof(geom)), -1,
                      SQLITE_STATIC);

  tg_geom_free(geom);
}

static void tg_point_impl(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  double x = sqlite3_value_double(argv[0]);
  double y = sqlite3_value_double(argv[1]);
  struct tg_point point = {x, y};
  struct tg_geom *geom = tg_geom_new_point(point);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultAndFreeGeomFormat(context, geom,
                          *((enum export_format *)sqlite3_user_data(context)));
}

#pragma endregion

#pragma entrypoint

#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_tg_init(sqlite3 *db, char **pzErrMsg,
                        const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  (void)pzErrMsg; /* Unused parameter */
#define DEFAULT_FLAGS (SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC)

  const char *debug =
      sqlite3_mprintf("Version: %s\nDate: %s\nSource: %s", SQLITE_TG_VERSION,
                      SQLITE_TG_DATE, SQLITE_TG_SOURCE);
  static enum export_format FORMAT_WKT = WKT;
  static enum export_format FORMAT_WKB = WKB;
  static enum export_format FORMAT_GEOJSON = GEOJSON;
  static enum export_format FORMAT_POINTER = POINTER;

  static const struct {

    char *zFName;
    int nArg;
    void (*xFunc)(sqlite3_context *, int, sqlite3_value **);
    void *pAux;
    void (*xDestroy)(void *);
    int flags;

  } aFunc[] = {
      // clang-format off
      {(char *)"tg_version",        0, tg_version,    NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_intersects",     2, tg_intersects, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_type",           1, tg_type,       NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_to_wkt",         1, tg_to_wkt,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_wkb",         1, tg_to_wkb,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_geojson",     1, tg_to_geojson, NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_point",          2, tg_point_impl, &FORMAT_POINTER,  NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_wkt",      2, tg_point_impl, &FORMAT_WKT,      NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_wkb",      2, tg_point_impl, &FORMAT_WKB,      NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_geojson",  2, tg_point_impl, &FORMAT_GEOJSON,  NULL,         DEFAULT_FLAGS},
      // clang-format on

  };
  for (int i = 0; i < sizeof(aFunc) / sizeof(aFunc[0]) && rc == SQLITE_OK;
       i++) {
    rc = sqlite3_create_function_v2(db, aFunc[i].zFName, aFunc[i].nArg,
                                    aFunc[i].flags, aFunc[i].pAux,
                                    aFunc[i].xFunc, 0, 0, 0);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  rc = sqlite3_create_function_v2(db, "tg_debug", 0, DEFAULT_FLAGS,
                                  (void *)debug, tg_debug, 0, 0, sqlite3_free);
  return rc;
}

#pragma endregion

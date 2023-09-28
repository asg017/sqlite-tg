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

static struct tg_geom *geomValue(sqlite3_value *value, int *needsFree) {
  int n = sqlite3_value_bytes(value);
  (*needsFree) = 1;
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
      (*needsFree) = 0;
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
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomWkt(context, geom);
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_to_wkb(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomWkb(context, geom);
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_to_geojson(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomGeojson(context, geom);
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_intersects(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_intersects(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
/*
static void tg_disjoint(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_disjoint(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
static void tg_contains(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_contains(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
static void tg_within(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_within(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
static void tg_covers(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_covers(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
static void tg_coveredby(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_coveredby(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
static void tg_touches(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFreeA = 0;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA);
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    return;
  }
  int needsFreeB = 0;
  struct tg_geom *b = geomValue(argv[1], &needsFreeB);
  if (tg_geom_error(b)) {
    if (needsFreeA)
      tg_geom_free(a);
    sqlite3_result_error(context, tg_geom_error(b), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_touches(a, b));

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}
*/

static void tg_valid_geojson(sqlite3_context *context, int argc,
                             sqlite3_value **argv) {
  // TODO it's text/blob?
  int n = sqlite3_value_bytes(argv[0]);
  const char *s = (const char *)sqlite3_value_text(argv[0]);
  struct tg_geom *geom = tg_parse_geojsonn_ix(s, n, TG_NONE);
  if (tg_geom_error(geom)) {
    sqlite3_result_int(context, 0);
  } else {
    sqlite3_result_int(context, 1);
    tg_geom_free(geom);
  }
}
static void tg_valid_wkt(sqlite3_context *context, int argc,
                         sqlite3_value **argv) {
  // TODO it's text/blob?
  int n = sqlite3_value_bytes(argv[0]);
  const char *s = (const char *)sqlite3_value_text(argv[0]);
  struct tg_geom *geom = tg_parse_wktn_ix(s, n, TG_NONE);
  if (tg_geom_error(geom)) {
    sqlite3_result_int(context, 0);
  } else {
    sqlite3_result_int(context, 1);
    tg_geom_free(geom);
  }
}
static void tg_valid_wkb(sqlite3_context *context, int argc,
                         sqlite3_value **argv) {
  // TODO ensure blob?
  int n = sqlite3_value_bytes(argv[0]);
  const void *b = sqlite3_value_blob(argv[0]);
  struct tg_geom *geom = tg_parse_wkb_ix(b, n, TG_NONE);
  if (tg_geom_error(geom)) {
    sqlite3_result_int(context, 0);
  } else {
    sqlite3_result_int(context, 1);
    tg_geom_free(geom);
  }
}
static void tg_geom(sqlite3_context *context, int argc, sqlite3_value **argv) {
  int n = sqlite3_value_bytes(argv[0]);
  enum tg_index index = TG_NONE;
  struct tg_geom *geom = NULL;
  if (argc > 1) {
    const char *value = (const char *)sqlite3_value_text(argv[1]);
    if (sqlite3_stricmp("none", value))
      index = TG_NONE;
    else if (sqlite3_stricmp("natural", value))
      index = TG_NATURAL;
    else if (sqlite3_stricmp("ystripes", value))
      index = TG_YSTRIPES;
    else {
      sqlite3_result_error(
          context,
          "unrecognized index option. Should be one of none/natural/ystripes",
          -1);
      return;
    }
  }
  switch (sqlite3_value_type(argv[0])) {
  case SQLITE_BLOB: {
    geom = tg_parse_wkb_ix(sqlite3_value_blob(argv[0]), n, index);
    break;
  }
  case SQLITE_TEXT: {
    const char *text = (const char *)sqlite3_value_text(argv[0]);
    if (sqlite3_value_subtype(argv[0]) == JSON_SUBTYPE ||
        // JSON is many SQL queries don't have a subtype,
        // so to distinguish between WKT/geojson, see if it
        // starts with "{", which almost always is geojson
        (n > 0 && text[0] == '{')) {
      geom = tg_parse_geojsonn_ix(text, n, index);
    } else {
      geom = tg_parse_wktn_ix(text, n, index);
    }
    break;
  }
  default:
    sqlite3_result_error(context,
                         "tg_geom() input must be WKT, WKB, or GeoJSON", -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_pointer(context, geom, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}
static void tg_type(sqlite3_context *context, int argc, sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_text(context, tg_geom_type_string(tg_geom_typeof(geom)), -1,
                      SQLITE_STATIC);

  if (needsFree)
    tg_geom_free(geom);
}

static void tg_extra_json(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  const char *result = tg_geom_extra_json(geom);
  if (result) {
    // TODO what if null character in json?
    sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
    sqlite3_result_subtype(context, JSON_SUBTYPE);
  } else {
    sqlite3_result_null(context);
  }

  if (needsFree)
    tg_geom_free(geom);
}

static void tg_dims(sqlite3_context *context, int argc, sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_dims(geom));
  if (needsFree)
    tg_geom_free(geom);
}
static void tg_is_empty(sqlite3_context *context, int argc,
                        sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_is_empty(geom));
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_is_feature(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_is_feature(geom));
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_is_featurecollection(sqlite3_context *context, int argc,
                                    sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_is_featurecollection(geom));
  if (needsFree)
    tg_geom_free(geom);
}
static void tg_memsize(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  sqlite3_result_int(context, tg_geom_memsize(geom));
  if (needsFree)
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
  // TODO wut
  resultAndFreeGeomFormat(context, geom,
                          *((enum export_format *)sqlite3_user_data(context)));
}

#pragma endregion

#pragma region each table functions

struct control {
  char *name;
  void *(*xValue)(sqlite3_value *value, int *needsFree);
  void (*xFree)(void *p);
  int (*xEof)(void *p, sqlite3_int64 iRowid);
  void (*xResult)(sqlite3_context *context, void *p, sqlite3_int64 iRowid);
};

#pragma region tg_points_each
void *pointsEachValue(sqlite3_value *value, int *needsFree) {
  struct tg_geom *geom = geomValue(value, needsFree);
  return (void *)geom;
}
int pointsEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_points((struct tg_geom *)p);
}
void pointsEachResult(sqlite3_context *context, void *p, sqlite3_int64 iRowid) {
  struct tg_point point = tg_geom_point_at((struct tg_geom *)p, iRowid);
  struct tg_geom *g = tg_geom_new_point(point);
  sqlite3_result_pointer(context, g, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}

void pointsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_lines_each
void *linesEachValue(sqlite3_value *value, int *needsFree) {
  struct tg_geom *geom = geomValue(value, needsFree);
  return (void *)geom;
}
int linesEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_lines((struct tg_geom *)p);
}
void linesEachResult(sqlite3_context *context, void *p, sqlite3_int64 iRowid) {
  const struct tg_line *line = tg_geom_line_at((struct tg_geom *)p, iRowid);
  sqlite3_result_pointer(context, (void *)line, TG_GEOM_POINTER_NAME, NULL);
}

void linesEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_geometries_each
void *geometriesEachValue(sqlite3_value *value, int *needsFree) {
  struct tg_geom *geom = geomValue(value, needsFree);
  return (void *)geom;
}
int geometriesEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_geometries((struct tg_geom *)p);
}
void geometriesEachResult(sqlite3_context *context, void *p,
                          sqlite3_int64 iRowid) {
  const struct tg_geom *geom = tg_geom_geometry_at((struct tg_geom *)p, iRowid);
  sqlite3_result_pointer(context, (void *)geom, TG_GEOM_POINTER_NAME, NULL);
}

void geometriesEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_polygons_each
void *polygonsEachValue(sqlite3_value *value, int *needsFree) {
  struct tg_geom *geom = geomValue(value, needsFree);
  return (void *)geom;
}
int polygonsEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_polys((struct tg_geom *)p);
}
void polygonsEachResult(sqlite3_context *context, void *p,
                        sqlite3_int64 iRowid) {
  const struct tg_poly *poly = tg_geom_poly_at((struct tg_geom *)p, iRowid);

  // rationale: tg_poly can safely upcast to tg_geom
  // https://github.com/tidwall/tg/blob/main/docs/API.md#upcasting
  sqlite3_result_pointer(context, (void *)(void *)poly, TG_GEOM_POINTER_NAME,
                         NULL);
}

void polygonsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

typedef struct template_each_vtab template_each_vtab;
struct template_each_vtab {
  sqlite3_vtab base;
  struct control *pControl;
};

typedef struct template_each_cursor template_each_cursor;
struct template_each_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
  void *target;
  int targetNeedsFree;
};

static int template_eachConnect(sqlite3 *db, void *pAux, int argc,
                                const char *const *argv, sqlite3_vtab **ppVtab,
                                char **pzErr) {
  template_each_vtab *pNew;
  int rc;

  struct control *pControl = (struct control *)pAux;
  const char *schema =
      sqlite3_mprintf("CREATE TABLE x(%w, source hidden)", pControl->name);
#define TEMPLATE_EACH_TARGET 0
#define TEMPLATE_EACH_SOURCE 1
  rc = sqlite3_declare_vtab(db, schema);
  sqlite3_free((void *)schema);
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    pNew->pControl = pControl;
  }
  return rc;
}

static int template_eachDisconnect(sqlite3_vtab *pVtab) {
  template_each_vtab *p = (template_each_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int template_eachOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  template_each_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int template_eachClose(sqlite3_vtab_cursor *cur) {
  template_each_cursor *pCur = (template_each_cursor *)cur;
  if (pCur->target) {
    struct control *pControl = ((template_each_vtab *)cur->pVtab)->pControl;
    pControl->xFree(pCur->target);
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int template_eachBestIndex(sqlite3_vtab *pVTab,
                                  sqlite3_index_info *pIdxInfo) {
  int hasSource = 0;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    switch (pCons->iColumn) {
    case TEMPLATE_EACH_SOURCE: {
      if (!hasSource && !pCons->usable ||
          pCons->op != SQLITE_INDEX_CONSTRAINT_EQ)
        return SQLITE_CONSTRAINT;
      hasSource = 1;
      pIdxInfo->aConstraintUsage[i].argvIndex = 1;
      pIdxInfo->aConstraintUsage[i].omit = 1;
    }
    }
  }
  if (!hasSource) {
    pVTab->zErrMsg = sqlite3_mprintf("source argument is required");
    return SQLITE_ERROR;
  }

  pIdxInfo->idxNum = 1;
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

static int template_eachFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                               const char *idxStr, int argc,
                               sqlite3_value **argv) {
  template_each_cursor *pCur = (template_each_cursor *)pVtabCursor;
  pCur->iRowid = 0;
  struct control *pControl =
      ((template_each_vtab *)pVtabCursor->pVtab)->pControl;
  if (pCur->target && pCur->targetNeedsFree) {
    pControl->xFree(pCur->target);
    pCur->targetNeedsFree = 0;
  }
  pCur->target = pControl->xValue(argv[0], &pCur->targetNeedsFree);
  return SQLITE_OK;
}

static int template_eachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  template_each_cursor *pCur = (template_each_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int template_eachNext(sqlite3_vtab_cursor *cur) {
  template_each_cursor *pCur = (template_each_cursor *)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

static int template_eachEof(sqlite3_vtab_cursor *cur) {
  template_each_cursor *pCur = (template_each_cursor *)cur;
  struct control *pControl = ((template_each_vtab *)cur->pVtab)->pControl;
  return pControl->xEof(pCur->target, pCur->iRowid);
}

static int template_eachColumn(sqlite3_vtab_cursor *cur,
                               sqlite3_context *context, int i) {
  template_each_cursor *pCur = (template_each_cursor *)cur;
  switch (i) {
  case TEMPLATE_EACH_TARGET: {
    struct control *pControl =
        ((template_each_vtab *)pCur->base.pVtab)->pControl;
    pControl->xResult(context, pCur->target, pCur->iRowid);
    break;
  }
  }
  return SQLITE_OK;
}

static sqlite3_module template_eachModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ template_eachConnect,
    /* xBestIndex  */ template_eachBestIndex,
    /* xDisconnect */ template_eachDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ template_eachOpen,
    /* xClose      */ template_eachClose,
    /* xFilter     */ template_eachFilter,
    /* xNext       */ template_eachNext,
    /* xEof        */ template_eachEof,
    /* xColumn     */ template_eachColumn,
    /* xRowid      */ template_eachRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};
#pragma endregion

#pragma region tg_rect_parts() table function

typedef struct tg_rect_parts_vtab tg_rect_parts_vtab;
struct tg_rect_parts_vtab {
  sqlite3_vtab base;
};

typedef struct tg_rect_parts_cursor tg_rect_parts_cursor;
struct tg_rect_parts_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
  struct tg_rect rect;
};

static int tg_rect_partsConnect(sqlite3 *db, void *pAux, int argc,
                                const char *const *argv, sqlite3_vtab **ppVtab,
                                char **pzErr) {
  tg_rect_parts_vtab *pNew;
  int rc;
#define TG_RECT_PARTS_MINX 0
#define TG_RECT_PARTS_MAXX 1
#define TG_RECT_PARTS_MINY 2
#define TG_RECT_PARTS_MAXY 3
#define TG_RECT_PARTS_SOURCE 4
  rc = sqlite3_declare_vtab(
      db, "CREATE TABLE x(minX, maxX, minY, maxY, source hidden)");
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

static int tg_rect_partsDisconnect(sqlite3_vtab *pVtab) {
  tg_rect_parts_vtab *p = (tg_rect_parts_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int tg_rect_partsOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  tg_rect_parts_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int tg_rect_partsClose(sqlite3_vtab_cursor *cur) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int tg_rect_partsBestIndex(sqlite3_vtab *pVTab,
                                  sqlite3_index_info *pIdxInfo) {
  int hasSource = 0;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    switch (pCons->iColumn) {
    case TG_RECT_PARTS_SOURCE: {
      if (!hasSource && !pCons->usable ||
          pCons->op != SQLITE_INDEX_CONSTRAINT_EQ)
        return SQLITE_CONSTRAINT;
      hasSource = 1;
      pIdxInfo->aConstraintUsage[i].argvIndex = 1;
      pIdxInfo->aConstraintUsage[i].omit = 1;
    }
    }
  }
  if (!hasSource) {
    pVTab->zErrMsg = sqlite3_mprintf("source argument is required");
    return SQLITE_ERROR;
  }

  pIdxInfo->idxNum = 1;
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

static int tg_rect_partsFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                               const char *idxStr, int argc,
                               sqlite3_value **argv) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)pVtabCursor;
  pCur->iRowid = 0;
  int needsFree = 0;
  struct tg_geom *geom = geomValue(argv[0], &needsFree);
  pCur->rect = tg_geom_rect(geom);
  if (needsFree)
    tg_geom_free(geom);
  return SQLITE_OK;
}

static int tg_rect_partsRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int tg_rect_partsNext(sqlite3_vtab_cursor *cur) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

static int tg_rect_partsEof(sqlite3_vtab_cursor *cur) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)cur;
  return pCur->iRowid >= 1;
}

static int tg_rect_partsColumn(sqlite3_vtab_cursor *cur,
                               sqlite3_context *context, int i) {
  tg_rect_parts_cursor *pCur = (tg_rect_parts_cursor *)cur;
  switch (i) {
  case TG_RECT_PARTS_MINX:
    sqlite3_result_double(context, pCur->rect.min.x);
    break;
  case TG_RECT_PARTS_MAXX:
    sqlite3_result_double(context, pCur->rect.max.x);
    break;
  case TG_RECT_PARTS_MINY:
    sqlite3_result_double(context, pCur->rect.min.y);
    break;
  case TG_RECT_PARTS_MAXY:
    sqlite3_result_double(context, pCur->rect.max.y);
    break;
  case TG_RECT_PARTS_SOURCE:
    // TODO resolve back to original geometry?
    sqlite3_result_null(context);
    break;
  }
  return SQLITE_OK;
}

static sqlite3_module tg_rect_partsModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ tg_rect_partsConnect,
    /* xBestIndex  */ tg_rect_partsBestIndex,
    /* xDisconnect */ tg_rect_partsDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ tg_rect_partsOpen,
    /* xClose      */ tg_rect_partsClose,
    /* xFilter     */ tg_rect_partsFilter,
    /* xNext       */ tg_rect_partsNext,
    /* xEof        */ tg_rect_partsEof,
    /* xColumn     */ tg_rect_partsColumn,
    /* xRowid      */ tg_rect_partsRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};
#pragma endregion

#pragma region entrypoint

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
      //{(char *)"tg_disjoint",       2, tg_disjoint,   NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_contains",       2, tg_contains,   NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_within",         2, tg_within,     NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_covers",         2, tg_covers,     NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_coveredby",      2, tg_coveredby,  NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_touches",        2, tg_touches,    NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_geom",           1, tg_geom,                 NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_geom",           2, tg_geom,                 NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_type",           1, tg_type,                 NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_extra_json",     1, tg_extra_json,           NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_valid_geojson",  1, tg_valid_geojson,       NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_valid_wkb",      1, tg_valid_wkb,           NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_valid_wkt",      1, tg_valid_wkt,           NULL,             NULL,         DEFAULT_FLAGS},

      //{(char *)"tg_dims",                 1, tg_dims,                 NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_is_empty",             1, tg_is_empty,             NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_is_feature",           1, tg_is_feature,           NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_is_featurecollection", 1, tg_is_featurecollection, NULL,             NULL,         DEFAULT_FLAGS},
      //{(char *)"tg_memsize",     1, tg_memsize, NULL,             NULL,         DEFAULT_FLAGS},

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

  static struct control pointsEach = {
      .name = "point",
      .xValue = &pointsEachValue,
      .xEof = &pointsEachEof,
      .xResult = &pointsEachResult,
      .xFree = &pointsEachFree,
  };
  static struct control linesEach = {
      .name = "line",
      .xValue = &linesEachValue,
      .xEof = &linesEachEof,
      .xResult = &linesEachResult,
      .xFree = &linesEachFree,
  };
  static struct control geometriesEach = {
      .name = "geometry",
      .xValue = &geometriesEachValue,
      .xEof = &geometriesEachEof,
      .xResult = &geometriesEachResult,
      .xFree = &geometriesEachFree,
  };
  static struct control polygonsEach = {
      .name = "polygon",
      .xValue = &polygonsEachValue,
      .xEof = &polygonsEachEof,
      .xResult = &polygonsEachResult,
      .xFree = &polygonsEachFree,
  };

  static const struct {
    char *zModuleName;
    struct control *c;

  } aEachModules[] = {
      // clang-format off
      {(char *)"tg_points_each",  &pointsEach},
      {(char *)"tg_lines_each",  &linesEach},
      {(char *)"tg_polygons_each",  &polygonsEach},
      {(char *)"tg_geometries_each",  &geometriesEach},
      // clang-format on
  };

  for (int i = 0; i < sizeof(aEachModules) / sizeof(aEachModules[0]); i++) {
    rc = sqlite3_create_module(db, aEachModules[i].zModuleName,
                               &template_eachModule, aEachModules[i].c);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

  rc = sqlite3_create_module(db, "tg_rect_parts", &tg_rect_partsModule, NULL);
  if (rc != SQLITE_OK)
    return rc;
  rc = sqlite3_create_function_v2(db, "tg_debug", 0, DEFAULT_FLAGS,
                                  (void *)debug, tg_debug, 0, 0, sqlite3_free);
  if (rc != SQLITE_OK)
    return rc;

  return rc;
}

#pragma endregion

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

#define INVALID_GEO_INPUT                                                      \
  "invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON "  \
  "(as text)."

static struct tg_geom *geomValue(sqlite3_value *value, int *needsFree,
                                 int *invalid) {
  int n = sqlite3_value_bytes(value);
  (*invalid) = 0;
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
  (*invalid) = 1;
  return NULL;
}

#pragma endregion

#pragma region resulting

static void destroy_geom(void *p) { tg_geom_free(p); }

static void resultGeomPointer(sqlite3_context *context, struct tg_geom *geom) {
  sqlite3_result_pointer(context, geom, TG_GEOM_POINTER_NAME, destroy_geom);
}


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

#pragma endregion

#pragma region meta functions
static void tg_version(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  sqlite3_result_text(context, SQLITE_TG_VERSION, -1, SQLITE_STATIC);
}

static void tg_debug(sqlite3_context *context, int argc, sqlite3_value **arg) {
  sqlite3_result_text(context, sqlite3_user_data(context), -1, SQLITE_STATIC);
}

#pragma endregion

#pragma region conversions

static void tg_to_wkt(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  int needsFree, invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
  } else {
    resultGeomWkt(context, geom);
  }
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_to_wkb(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  int needsFree, invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
  } else {
    resultGeomWkb(context, geom);
  }
  if (needsFree)
    tg_geom_free(geom);
}

static void tg_to_geojson(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFree, invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
  } else {
    resultGeomGeojson(context, geom);
  }
  if (needsFree)
    tg_geom_free(geom);
}

#pragma endregion

#pragma region predicates

typedef bool (*GeomPredicateFunc)(const struct tg_geom *a,
                                  const struct tg_geom *b);

static void tg_predicate_impl(sqlite3_context *context, int argc,
                              sqlite3_value **argv) {
  int needsFreeA = 0;
  int needsFreeB = 0;
  int invalid;
  struct tg_geom *a = geomValue(argv[0], &needsFreeA, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(a)) {
    sqlite3_result_error(context, tg_geom_error(a), -1);
    if (needsFreeA)
      tg_geom_free(a);
    return;
  }
  struct tg_geom *b = geomValue(argv[1], &needsFreeB, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    if (needsFreeA)
      tg_geom_free(a);
    return;
  }
  if (tg_geom_error(b)) {
    sqlite3_result_error(context, tg_geom_error(b), -1);
  } else {
    GeomPredicateFunc func = sqlite3_user_data(context);
    sqlite3_result_int(context, func(a, b));
  }

  if (needsFreeA)
    tg_geom_free(a);
  if (needsFreeB)
    tg_geom_free(b);
}

#pragma endregion

#pragma region validators

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
  }
  tg_geom_free(geom);
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
  }
  tg_geom_free(geom);
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
  }
  tg_geom_free(geom);
}

#pragma endregion

#pragma region tg_geom()

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
    tg_geom_free(geom);
    return;
  }
  sqlite3_result_pointer(context, geom, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}
static void tg_type(sqlite3_context *context, int argc, sqlite3_value **argv) {
  int needsFree = 0;
  int invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
  } else {
    sqlite3_result_text(context, tg_geom_type_string(tg_geom_typeof(geom)), -1,
                        SQLITE_STATIC);
  }

  if (needsFree)
    tg_geom_free(geom);
}

static void tg_extra_json(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  int needsFree = 0;
  int invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
  } else {
    const char *result = tg_geom_extra_json(geom);
    if (result) {
      // TODO what if null character in json?
      sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
      sqlite3_result_subtype(context, JSON_SUBTYPE);
    } else {
      sqlite3_result_null(context);
    }
  }

  if (needsFree)
    tg_geom_free(geom);
}
#pragma endregion

#pragma region constructors

static void tg_point(sqlite3_context *context, int argc, sqlite3_value **argv) {
  int typeX = sqlite3_value_type(argv[0]);
  int typeY = sqlite3_value_type(argv[1]);
  if (typeX != SQLITE_INTEGER && typeX != SQLITE_FLOAT) {
    sqlite3_result_error(context, "point X value must be an integer or float",
                         -1);
    return;
  }
  if (typeY != SQLITE_INTEGER && typeY != SQLITE_FLOAT) {
    sqlite3_result_error(context, "point Y value must be an integer or float",
                         -1);
    return;
  }
  double x = sqlite3_value_double(argv[0]);
  double y = sqlite3_value_double(argv[1]);
  struct tg_point point = {x, y};
  struct tg_geom *geom = tg_geom_new_point(point);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    tg_geom_free(geom);
    return;
  }
  resultGeomPointer(context, geom);
}

static void tg_multipoint(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_multipoint_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }

  struct tg_point *points = sqlite3_malloc(argc * sizeof(struct tg_point));
  if (!points) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memset(points, 0, argc * sizeof(struct tg_point));

  for (int i = 0; i < argc; i++) {
    int needsFree;
    int invalid;
    struct tg_geom *geom = geomValue(argv[i], &needsFree, &invalid);
    if (invalid) {
      sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
      sqlite3_free(points);
      return;
    }
    if (tg_geom_error(geom)) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipoint() at index %i is an invalid geometry", i);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);

      if (needsFree) {
        tg_geom_free(geom);
      }
      sqlite3_free(points);
      return;
    }
    if (tg_geom_typeof(geom) != TG_POINT) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipoint() at index %i expected a point, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      if (needsFree) {
        tg_geom_free(geom);
      }
      sqlite3_free(points);
      return;
    }
    struct tg_point src = tg_geom_point(geom);
    struct tg_point *dest = &points[i];
    dest->x = src.x;
    dest->y = src.y;
    if (needsFree) {
      tg_geom_free(geom);
    }
  }
  struct tg_geom *geom = tg_geom_new_multipoint(points, argc);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
  sqlite3_free(points);
}

#pragma region tg_group_multipoint
typedef struct MultiPointContext MultiPointContext;
struct MultiPointContext {
  unsigned capacity;
  unsigned length;
  struct tg_point *array;
};

static void tg_multipoint_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  MultiPointContext *pContext = (MultiPointContext *)sqlite3_aggregate_context(
      context, sizeof(MultiPointContext));
  if (!pContext) {
    return;
  }
  if (pContext->length >= pContext->capacity) {
    unsigned newCapacity = pContext->capacity * 2 + 250;
    struct tg_point *a = sqlite3_realloc64(
        pContext->array, sizeof(struct tg_point) * newCapacity);
    if (a == 0) {
      sqlite3_free(pContext->array);
      memset(pContext, 0, sizeof(*pContext));
      sqlite3_result_error_nomem(context);
      return;
    }
    pContext->capacity = newCapacity;
    pContext->array = a;
  }

  int needsFree;
  int invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    tg_geom_free(geom);
    return;
  }
  if (tg_geom_typeof(geom) != TG_POINT) {
    sqlite3_result_error(
        context, "parameters to tg_group_multipoint() must be Point gemetries",
        -1);
    tg_geom_free(geom);
    return;
  }
  struct tg_point src = tg_geom_point(geom);
  struct tg_point *dest = &pContext->array[pContext->length++];
  dest->x = src.x;
  dest->y = src.y;

  if (needsFree) {
    tg_geom_free(geom);
  }
}

static void tg_multipoint_final(sqlite3_context *context) {
  MultiPointContext *p;
  p = (MultiPointContext *)sqlite3_aggregate_context(context, 0);
  if (p == 0)
    return;
  if (p->array == 0)
    return;
  if (p->length) {
    struct tg_geom *geom = tg_geom_new_multipoint(p->array, p->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }
  sqlite3_free(p->array);
  memset(p, 0, sizeof(*p));
}
#pragma endregion

#pragma region tg_group_geometrycollection
typedef struct GeometryCollectionContext GeometryCollectionContext;
struct GeometryCollectionContext {
  unsigned capacity;
  unsigned length;
  struct tg_geom **array;
};

static void tg_group_geometrycollection_step(sqlite3_context *context, int argc,
                                             sqlite3_value *argv[]) {

  GeometryCollectionContext *pContext =
      (GeometryCollectionContext *)sqlite3_aggregate_context(
          context, sizeof(GeometryCollectionContext));
  if (!pContext) {
    return;
  }
  if (pContext->length >= pContext->capacity) {
    unsigned newCapacity = pContext->capacity * 2 + 250;
    struct tg_geom **a = sqlite3_realloc64(
        pContext->array, sizeof(struct tg_geom *) * newCapacity);
    if (a == 0) {
      sqlite3_free(pContext->array);
      memset(pContext, 0, sizeof(*pContext));
      sqlite3_result_error_nomem(context);
      return;
    }
    pContext->capacity = newCapacity;
    pContext->array = a;
  }

  int needsFree;
  int invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    sqlite3_result_error(context, INVALID_GEO_INPUT, -1);
    return;
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    tg_geom_free(geom);
    return;
  }

  if (needsFree) {
    // printf("adding i=%d geom=%p\n", pContext->length, geom);
    pContext->array[pContext->length++] = tg_geom_copy(geom);
    tg_geom_free(geom);
  } else {
    // struct tg_geom * t = tg_geom_clone(geom);
    // printf("adding i=%d geom=%p (clone)\n", pContext->length, t);
    pContext->array[pContext->length++] = tg_geom_copy(geom);
  }
}

static void tg_group_geometrycollection_final(sqlite3_context *context) {
  GeometryCollectionContext *p;
  p = (GeometryCollectionContext *)sqlite3_aggregate_context(context, 0);
  if (p == 0)
    return;
  if (p->array == 0)
    return;
  if (p->length) {
    struct tg_geom *geom = tg_geom_new_geometrycollection(p->array, p->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }
  for (int i = 0; i < p->length; i++) {
    tg_geom_free(p->array[i]);
  }
  sqlite3_free(p->array);
  memset(p, 0, sizeof(*p));
}
#pragma endregion

#pragma endregion

#pragma region each table functions

struct control {
  char *name;
  void *(*xValue)(sqlite3_value *value, int *needsFree, int *invalid);
  void (*xFree)(void *p);
  int (*xEof)(void *p, sqlite3_int64 iRowid);
  void (*xResult)(sqlite3_context *context, void *p, sqlite3_int64 iRowid);
};

#pragma region tg_points_each
void *pointsEachValue(sqlite3_value *value, int *needsFree, int *invalid) {
  struct tg_geom *geom = geomValue(value, needsFree, invalid);
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
void *linesEachValue(sqlite3_value *value, int *needsFree, int *invalid) {
  struct tg_geom *geom = geomValue(value, needsFree, invalid);
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
void *geometriesEachValue(sqlite3_value *value, int *needsFree, int *invalid) {
  struct tg_geom *geom = geomValue(value, needsFree, invalid);
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
void *polygonsEachValue(sqlite3_value *value, int *needsFree, int *invalid) {
  struct tg_geom *geom = geomValue(value, needsFree, invalid);
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
  if (pCur->target && pCur->targetNeedsFree) {
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
    pCur->target = 0;
  }
  int invalid;
  pCur->target = pControl->xValue(argv[0], &pCur->targetNeedsFree, &invalid);
  if (invalid) {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
    return SQLITE_ERROR;
  }
  // TODO need to generalize it if line/ring support?
  if (tg_geom_error(pCur->target)) {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg =
        sqlite3_mprintf("%s", tg_geom_error(pCur->target));
    // No need to free pCur->target here, the xClose method will do taht
    return SQLITE_ERROR;
  }
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

#pragma region tg_bbox() table function

typedef struct tg_bbox_vtab tg_bbox_vtab;
struct tg_bbox_vtab {
  sqlite3_vtab base;
};

typedef struct tg_bbox_cursor tg_bbox_cursor;
struct tg_bbox_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
  struct tg_rect rect;
};

static int tg_bboxConnect(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
  tg_bbox_vtab *pNew;
  int rc;
#define TG_BBOX_MINX 0
#define TG_BBOX_MAXX 1
#define TG_BBOX_MINY 2
#define TG_BBOX_MAXY 3
#define TG_BBOX_SOURCE 4
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

static int tg_bboxDisconnect(sqlite3_vtab *pVtab) {
  tg_bbox_vtab *p = (tg_bbox_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int tg_bboxOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  tg_bbox_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int tg_bboxClose(sqlite3_vtab_cursor *cur) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int tg_bboxBestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
  int hasSource = 0;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    switch (pCons->iColumn) {
    case TG_BBOX_SOURCE: {
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

static int tg_bboxFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                         const char *idxStr, int argc, sqlite3_value **argv) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)pVtabCursor;
  pCur->iRowid = 0;
  int needsFree, invalid;
  struct tg_geom *geom = geomValue(argv[0], &needsFree, &invalid);
  if (invalid) {
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
    return SQLITE_ERROR;
  }
  if (tg_geom_error(geom)) {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", tg_geom_error(geom));
    tg_geom_free(geom);
    return SQLITE_ERROR;
  }

  pCur->rect = tg_geom_rect(geom);

  if (needsFree)
    tg_geom_free(geom);
  return SQLITE_OK;
}

static int tg_bboxRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int tg_bboxNext(sqlite3_vtab_cursor *cur) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

static int tg_bboxEof(sqlite3_vtab_cursor *cur) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)cur;
  return pCur->iRowid >= 1;
}

static int tg_bboxColumn(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                         int i) {
  tg_bbox_cursor *pCur = (tg_bbox_cursor *)cur;
  switch (i) {
  case TG_BBOX_MINX:
    sqlite3_result_double(context, pCur->rect.min.x);
    break;
  case TG_BBOX_MAXX:
    sqlite3_result_double(context, pCur->rect.max.x);
    break;
  case TG_BBOX_MINY:
    sqlite3_result_double(context, pCur->rect.min.y);
    break;
  case TG_BBOX_MAXY:
    sqlite3_result_double(context, pCur->rect.max.y);
    break;
  case TG_BBOX_SOURCE:
    // TODO resolve back to original geometry?
    sqlite3_result_null(context);
    break;
  }
  return SQLITE_OK;
}

static sqlite3_module tg_bboxModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ tg_bboxConnect,
    /* xBestIndex  */ tg_bboxBestIndex,
    /* xDisconnect */ tg_bboxDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ tg_bboxOpen,
    /* xClose      */ tg_bboxClose,
    /* xFilter     */ tg_bboxFilter,
    /* xNext       */ tg_bboxNext,
    /* xEof        */ tg_bboxEof,
    /* xColumn     */ tg_bboxColumn,
    /* xRowid      */ tg_bboxRowid,
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

#pragma region tg0 virtual table

#define TG0_COLUMN_SHAPE 0
#define TG0_COLUMN_REST 1

#define TG0_SQL_RTREE_DROP "DROP TABLE \"%w\".\"%w_rtree\""
#define TG0_SQL_DELETE "DELETE FROM \"%w\".\"%w_rtree\" WHERE id = ?1"

// clang-format off
// The different overloaded and intercepted functions for tg0() tables.
#define TG0_FUNC_INTERSECTS SQLITE_INDEX_CONSTRAINT_FUNCTION
#define TG0_FUNC_DISJOINT   SQLITE_INDEX_CONSTRAINT_FUNCTION + 1
#define TG0_FUNC_CONTAINS   SQLITE_INDEX_CONSTRAINT_FUNCTION + 2
#define TG0_FUNC_WITHIN     SQLITE_INDEX_CONSTRAINT_FUNCTION + 3
#define TG0_FUNC_COVERS     SQLITE_INDEX_CONSTRAINT_FUNCTION + 4
#define TG0_FUNC_COVEREDBY  SQLITE_INDEX_CONSTRAINT_FUNCTION + 5
// clang-format on

enum TG0_PLAN {
  // full scan, admit all rtree entries
  FULLSCAN,
  // only emit boundaries that interect a query geometry
  INTERSECT,
  // TODO disjoint/contains/within/covers/coveredby
};

typedef struct tg0_vtab tg0_vtab;
struct tg0_vtab {
  sqlite3_vtab base;

  // the SQLite connection of the host database
  sqlite3 *db;
  // Name of the schema the table exists on. Must be freed with sqlite3_free()
  // on destruction
  char *schemaName;
  // Name of the table the table exists on. must be freed with sqlite3_free() on
  // destruction
  char *tableName;

  int numAuxColumns;
};

typedef struct tg0_cursor tg0_cursor;
struct tg0_cursor {
  sqlite3_vtab_cursor base;
  // a statement that iterates over some sort of `select id, _shape` query.
  sqlite3_stmt *stmt;
  // the result code of the most recent sqlite3_step() on stmt
  int stepStatus;
  // The type of tree query that should be made
  enum TG0_PLAN plan;
  // the "query geometry" in predicate-style queries.
  struct tg_geom *queryGeom;
  int queryGeomNeedsFree;
};

static int tg0_init(sqlite3 *db, void *pAux, int argc, const char *const *argv,
                    sqlite3_vtab **ppVtab, char **pzErr, bool isCreate) {
  tg0_vtab *pNew;
  int rc;
  sqlite3_str *strSchema = sqlite3_str_new(NULL);
  sqlite3_str_appendall(strSchema, "CREATE TABLE x(_shape");
  for (int i = 3; i < argc; i++) {
    sqlite3_str_appendf(strSchema, ", %w", argv[i]);
  }
  sqlite3_str_appendall(strSchema, ")");
  const char *zSchema = sqlite3_str_finish(strSchema);
  if (!zSchema) {
    return SQLITE_NOMEM;
  }

  rc = sqlite3_declare_vtab(db, zSchema);
  sqlite3_free((void *)zSchema);
  if (rc != SQLITE_OK) {
    return rc;
  }
  pNew = sqlite3_malloc(sizeof(*pNew));
  *ppVtab = (sqlite3_vtab *)pNew;
  if (pNew == 0)
    return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));
  const char *schemaName = argv[1];
  const char *tableName = argv[2];

  pNew->db = db;
  pNew->schemaName = sqlite3_mprintf("%s", schemaName);
  pNew->tableName = sqlite3_mprintf("%s", tableName);
  pNew->numAuxColumns = argc - 3;

  if (isCreate) {
    sqlite3_stmt *stmt;
    sqlite3_str *strRtreeSchema = sqlite3_str_new(NULL);
    sqlite3_str_appendf(strRtreeSchema,
                        "CREATE VIRTUAL TABLE \"%w\".\"%w_rtree\" using "
                        "rtree(id, minX, maxX, minY, maxY, +_shape BLOB",
                        schemaName, tableName);
    for (int i = 3; i < argc; i++) {
      sqlite3_str_appendf(strRtreeSchema, ", +c%d", i - 3 + 1);
    }
    sqlite3_str_appendall(strRtreeSchema, ")");
    const char *zCreate = sqlite3_str_finish(strRtreeSchema);
    if (!zCreate) {
      return SQLITE_NOMEM;
    }

    int rc = sqlite3_prepare_v2(db, zCreate, -1, &stmt, 0);
    sqlite3_free((void *)zCreate);

    if (rc != SQLITE_OK) {
      return SQLITE_ERROR;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      return SQLITE_ERROR;
    }
    sqlite3_finalize(stmt);
  }
  return SQLITE_OK;
}

static int tg0Create(sqlite3 *db, void *pAux, int argc, const char *const *argv,
                     sqlite3_vtab **ppVtab, char **pzErr) {
  return tg0_init(db, pAux, argc, argv, ppVtab, pzErr, true);
}
static int tg0Connect(sqlite3 *db, void *pAux, int argc,
                      const char *const *argv, sqlite3_vtab **ppVtab,
                      char **pzErr) {
  return tg0_init(db, pAux, argc, argv, ppVtab, pzErr, false);
}

static int tg0Disconnect(sqlite3_vtab *pVtab) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  sqlite3_free(p->schemaName);
  sqlite3_free(p->tableName);
  sqlite3_free(p);
  return SQLITE_OK;
}
static int tg0Destroy(sqlite3_vtab *pVtab) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  sqlite3_stmt *stmt;
  const char *zSql =
      sqlite3_mprintf(TG0_SQL_RTREE_DROP, p->schemaName, p->tableName);
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, 0);
  sqlite3_free((void *)zSql);

  if (rc == SQLITE_OK) {
    // ignore if there's an error?
    sqlite3_step(stmt);
  }

  sqlite3_finalize(stmt);
  tg0Disconnect(pVtab);
  return SQLITE_OK;
}

static int tg0Open(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  tg0_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int tg0Close(sqlite3_vtab_cursor *cur) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
  }
  if (pCur->queryGeomNeedsFree) {
    tg_geom_free(pCur->queryGeom);
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int tg0BestIndex(sqlite3_vtab *pVtab, sqlite3_index_info *pIdxInfo) {

  int iPredicateTerm = -1;
  int predicate = 0;

  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    if (!pIdxInfo->aConstraint[i].usable)
      continue;

    int iColumn = pIdxInfo->aConstraint[i].iColumn;
    int op = pIdxInfo->aConstraint[i].op;
    if (op == TG0_FUNC_INTERSECTS || op == TG0_FUNC_DISJOINT ||
        op == TG0_FUNC_CONTAINS || op == TG0_FUNC_WITHIN ||
        op == TG0_FUNC_COVERS || op == TG0_FUNC_COVEREDBY) {
      if (iPredicateTerm >= 0) {
        // TODO err msg, only 1 predicate at a time for now?
        sqlite3_free(pVtab->zErrMsg);
        pVtab->zErrMsg = sqlite3_mprintf(
            "only 1 predicate is allowed on tg0 WHERE clauses.");
        return SQLITE_ERROR;
      }
      iPredicateTerm = i;
      predicate = op;
    }
  }
  if (iPredicateTerm >= 0) {
    pIdxInfo->idxStr = (char *)"predicate";
    pIdxInfo->idxNum = predicate;
    pIdxInfo->aConstraintUsage[iPredicateTerm].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iPredicateTerm].omit = 1;
    pIdxInfo->estimatedCost = 30.0;
    pIdxInfo->estimatedRows = 10;
  } else {
    pIdxInfo->idxStr = (char *)"fullscan";
    pIdxInfo->estimatedCost = 3000000.0;
    pIdxInfo->estimatedRows = 100000;
  }

  return SQLITE_OK;
}

// forward delcaration bc tg0Filter uses it
static int tg0Next(sqlite3_vtab_cursor *cur);

static int tg0Filter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                     const char *idxStr, int argc, sqlite3_value **argv) {
  tg0_cursor *pCur = (tg0_cursor *)pVtabCursor;
  tg0_vtab *p = (tg0_vtab *)pVtabCursor->pVtab;

  // TODO need to check if there's a lingering pCur->stmt that needs finalizing,
  // or a lingering pCur->queryGeom that needs freeing
  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
    pCur->stmt = 0;
  }
  if (pCur->queryGeomNeedsFree) {
    tg_geom_free(pCur->queryGeom);
    pCur->queryGeomNeedsFree = 0;
  }

  if (strcmp(idxStr, "fullscan") == 0) {
    pCur->plan = FULLSCAN;
    sqlite3_str *strSqlFullscan = sqlite3_str_new(NULL);
    sqlite3_str_appendall(strSqlFullscan, "SELECT id, _shape");
    for (int i = 0; i < p->numAuxColumns; i++) {
      sqlite3_str_appendf(strSqlFullscan, ", c%d", i + 1);
    }
    sqlite3_str_appendf(strSqlFullscan, " FROM \"%w\".\"%w_rtree\"",
                        p->schemaName, p->tableName);

    const char *zSql = sqlite3_str_finish(strSqlFullscan);
    if (!zSql) {
      // TODO what needs to be freed here
      return SQLITE_NOMEM;
    }
    int rc = sqlite3_prepare_v2(p->db, zSql, -1, &pCur->stmt, NULL);
    sqlite3_free((void *)zSql);
    if (rc != SQLITE_OK) {
      sqlite3_free(pVtabCursor->pVtab->zErrMsg);
      pVtabCursor->pVtab->zErrMsg =
          sqlite3_mprintf("prep error: %s", sqlite3_errmsg(p->db));
      return SQLITE_ERROR;
    }
    return tg0Next(pVtabCursor);
  } else if (strcmp(idxStr, "predicate") == 0) {
    switch (idxNum) {
    case TG0_FUNC_INTERSECTS: {
      int invalid;
      pCur->plan = INTERSECT;
      pCur->queryGeom = geomValue(argv[0], &pCur->queryGeomNeedsFree, &invalid);
      if (invalid) {
        sqlite3_free(pVtabCursor->pVtab->zErrMsg);
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
        return SQLITE_ERROR;
      }
      struct tg_rect rect = tg_geom_rect(pCur->queryGeom);

      sqlite3_str *strSqlIntersect = sqlite3_str_new(NULL);
      sqlite3_str_appendall(strSqlIntersect, "SELECT id, _shape");
      for (int i = 0; i < p->numAuxColumns; i++) {
        sqlite3_str_appendf(strSqlIntersect, ", c%d", i + 1);
      }
      sqlite3_str_appendf(strSqlIntersect, " FROM \"%w\".\"%w_rtree\" as r ",
                          p->schemaName, p->tableName);
      sqlite3_str_appendall(strSqlIntersect, "WHERE r.minX <= ?1  "
                                             "AND r.maxX >= ?2 "
                                             "AND r.minY <= ?3 "
                                             "AND r.maxY >= ?4");

      const char *zSql = sqlite3_str_finish(strSqlIntersect);
      if (!zSql) {
        // TODO what needs to be freed here
        return SQLITE_NOMEM;
      }

      int rc = sqlite3_prepare_v2(p->db, zSql, -1, &pCur->stmt, NULL);
      sqlite3_free((void *)zSql);

      if (rc != SQLITE_OK) {
        sqlite3_free(pVtabCursor->pVtab->zErrMsg);
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("prep error");
        return SQLITE_ERROR;
      }
      sqlite3_bind_double(pCur->stmt, 1, rect.max.x);
      sqlite3_bind_double(pCur->stmt, 2, rect.min.x);
      sqlite3_bind_double(pCur->stmt, 3, rect.max.y);
      sqlite3_bind_double(pCur->stmt, 4, rect.min.y);
      break;
    }
    case TG0_FUNC_DISJOINT:
    case TG0_FUNC_CONTAINS:
    case TG0_FUNC_WITHIN:
    case TG0_FUNC_COVERS:
    case TG0_FUNC_COVEREDBY: {
      sqlite3_free(pVtabCursor->pVtab->zErrMsg);
      pVtabCursor->pVtab->zErrMsg =
          sqlite3_mprintf("The given predicate inside the WHERE clause on tg0 "
                          "table is not supported yet");
      return SQLITE_ERROR;
    }
    default: {
      sqlite3_free(pVtabCursor->pVtab->zErrMsg);
      pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("unknown query plan");
      return SQLITE_ERROR;
    }
    }
    return tg0Next(pVtabCursor);
  } else {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("unknown idxStr");
    return SQLITE_ERROR;
  }
}

static int tg0Rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  *pRowid = sqlite3_column_int64(pCur->stmt, 0);
  return SQLITE_OK;
}

static int tg0Next(sqlite3_vtab_cursor *cur) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  tg0_vtab *p = (tg0_vtab *)cur->pVtab;
  int stop = 0;
  while (!stop) {
    pCur->stepStatus = sqlite3_step(pCur->stmt);
    if (pCur->stepStatus == SQLITE_DONE) {
      break;
    }
    if (pCur->stepStatus != SQLITE_ROW) {
      sqlite3_free(cur->pVtab->zErrMsg);
      cur->pVtab->zErrMsg =
          sqlite3_mprintf("tg0Next step error: %d", pCur->stepStatus);
      return SQLITE_ERROR;
    }

    switch (pCur->plan) {
    case FULLSCAN: {
      stop = 1;
      break;
    }
    case INTERSECT: {

      int needsFree, invalid;
      struct tg_geom *geom =
          geomValue(sqlite3_column_value(pCur->stmt, 1), &needsFree, &invalid);
      if (invalid) {
        sqlite3_free(cur->pVtab->zErrMsg);
        cur->pVtab->zErrMsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
        return SQLITE_ERROR;
      }
      if (tg_geom_error(geom)) {
        sqlite3_free(cur->pVtab->zErrMsg);
        cur->pVtab->zErrMsg = sqlite3_mprintf("%s", tg_geom_error(geom));
        tg_geom_free(geom);
        return SQLITE_ERROR;
      }
      if (tg_geom_intersects(geom, pCur->queryGeom)) {
        stop = 1;
      } else {
        stop = 0;
      }
      if (needsFree) {
        tg_geom_free(geom);
      }
      break;
    }
    }
  }
  return SQLITE_OK;
}

static int tg0Eof(sqlite3_vtab_cursor *cur) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  return pCur->stepStatus != SQLITE_ROW;
}

static int tg0Column(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                     int i) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  if (i == TG0_COLUMN_SHAPE) {
    sqlite3_result_value(context, sqlite3_column_value(pCur->stmt, 1));
  } else if (i >= TG0_COLUMN_REST) {
    sqlite3_result_value(
        context, sqlite3_column_value(pCur->stmt, 2 + i - TG0_COLUMN_REST));
  }

  return SQLITE_OK;
}

static int tg0Update(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                     sqlite_int64 *pRowid) {

  tg0_vtab *p = (tg0_vtab *)pVTab;
  // DELETE operation
  if (argc == 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL) {
    sqlite3_int64 idToDelete = sqlite3_value_int64(argv[0]);
    sqlite3_stmt *stmt;
    sqlite3_free(pVTab->zErrMsg);
    char *zSql = sqlite3_mprintf(TG0_SQL_DELETE, p->schemaName, p->tableName);
    int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf("error preparing delete statement:  %s",
                                       sqlite3_errmsg(p->db));
      return SQLITE_ERROR;
    }
    sqlite3_bind_int64(stmt, 1, idToDelete);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf("error deleting rtree row: %s",
                                       sqlite3_errmsg(p->db));
    }
    sqlite3_finalize(stmt);
    return SQLITE_OK;
  }
  // INSERT operations
  else if (argc > 1 && sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    // TODO assert argc == 3

    int needsFree, invalid;
    struct tg_geom *geom =
        geomValue(argv[2 + TG0_COLUMN_SHAPE], &needsFree, &invalid);
    if (invalid) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
      return SQLITE_ERROR;
    }
    if (tg_geom_error(geom)) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf("%s", tg_geom_error(geom));
      if (needsFree)
        tg_geom_free(geom);
      return SQLITE_ERROR;
    }
    struct tg_rect rect = tg_geom_rect(geom);

    sqlite3_stmt *stmt;
    sqlite3_str *strInsert = sqlite3_str_new(NULL);
    bool hasRowid = sqlite3_value_type(argv[1]) != SQLITE_NULL;
    if (hasRowid) {
      sqlite3_str_appendf(
          strInsert,
          "INSERT INTO \"%w\".\"%w_rtree\"(id, minX, maxX, minY, maxY, _shape",
          p->schemaName, p->tableName);
    } else {
      sqlite3_str_appendf(
          strInsert,
          "INSERT INTO \"%w\".\"%w_rtree\"(minX, maxX, minY, maxY, _shape",
          p->schemaName, p->tableName);
    }
    for (int i = 0; i < p->numAuxColumns; i++) {
      sqlite3_str_appendf(strInsert, ", c%d", i + 1);
    }
    int paramStart;
    if (hasRowid) {
      paramStart = 1;
    } else {
      paramStart = 0;
    }
    if (hasRowid) {
      sqlite3_str_appendall(strInsert, ") VALUES (?1, ?2, ?3, ?4, ?5, ?6");
    } else {
      sqlite3_str_appendall(strInsert, ") VALUES (?1, ?2, ?3, ?4, ?5");
    }
    for (int i = 0; i < p->numAuxColumns; i++) {
      sqlite3_str_appendf(strInsert, ", ?%d", (6 + paramStart) + i);
    }
    sqlite3_str_appendall(strInsert, ")");

    const char *zSql = sqlite3_str_finish(strInsert);
    if (!zSql) {
      // TODO what needs to be freed here
      return SQLITE_NOMEM;
    }
    int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free((void *)zSql);

    if (rc != SQLITE_OK) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf(
          "tgoUpdate Error while preparing rtree insert statement:  %s",
          sqlite3_errmsg(p->db));
      if (needsFree)
        tg_geom_free(geom);
      return rc;
    }

    // WKB representation of the inserted geometry
    // TODO see if the input is already WKB and just use that
    size_t size = tg_geom_wkb(geom, 0, 0);
    void *buffer = sqlite3_malloc(size + 1);
    if (buffer == 0) {
      if (needsFree)
        tg_geom_free(geom);
      return SQLITE_NOMEM;
    }
    tg_geom_wkb(geom, buffer, size + 1);

    if (hasRowid) {
      rc = sqlite3_bind_int64(stmt, paramStart, sqlite3_value_int64(argv[1]));
    }
    rc = sqlite3_bind_double(stmt, paramStart + 1, rect.min.x);
    rc = sqlite3_bind_double(stmt, paramStart + 2, rect.max.x);
    rc = sqlite3_bind_double(stmt, paramStart + 3, rect.min.y);
    rc = sqlite3_bind_double(stmt, paramStart + 4, rect.max.y);
    rc = sqlite3_bind_blob(stmt, paramStart + 5, buffer, size, sqlite3_free);
    for (int i = 0; i < p->numAuxColumns; i++) {
      rc = sqlite3_bind_value(stmt, paramStart + 6 + i,
                              argv[2 + TG0_COLUMN_REST + i]);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg =
          sqlite3_mprintf("stepping not done? %s", sqlite3_errmsg(p->db));
      if (needsFree)
        tg_geom_free(geom);
      sqlite3_finalize(stmt);
      return SQLITE_ERROR;
    }
    sqlite3_finalize(stmt);
    if (needsFree)
      tg_geom_free(geom);
    return SQLITE_OK;
  }
  // some sort of UPDATE operation
  else {
    sqlite3_free(pVTab->zErrMsg);
    pVTab->zErrMsg =
        sqlite3_mprintf("UPDATE operation on tg0 not supported yet.");
    return SQLITE_ERROR;
  }
}

static int tg0FindFunction(sqlite3_vtab *pVtab, int nArg, const char *zName,
                           void (**pxFunc)(sqlite3_context *, int,
                                           sqlite3_value **),
                           void **ppArg) {
  if (sqlite3_stricmp(zName, "tg_intersects") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_intersects;
    return TG0_FUNC_INTERSECTS;
  }
  if (sqlite3_stricmp(zName, "tg_disjoint") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_disjoint;
    return TG0_FUNC_DISJOINT;
  }
  if (sqlite3_stricmp(zName, "tg_contains") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_contains;
    return TG0_FUNC_CONTAINS;
  }
  if (sqlite3_stricmp(zName, "tg_within") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_within;
    return TG0_FUNC_WITHIN;
  }
  if (sqlite3_stricmp(zName, "tg_covers") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_covers;
    return TG0_FUNC_COVERS;
  }
  if (sqlite3_stricmp(zName, "tg_coveredby") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_coveredby;
    return TG0_FUNC_COVEREDBY;
  }
  return 0;
};

static int tg0ShadowName(const char *zName) {

  static const char *azName[] = {"rtree", "rtree_node", "rtree_parent",
                                 "rtree_rowid"};

  for (int i = 0; i < sizeof(azName) / sizeof(azName[0]); i++) {
    if (sqlite3_stricmp(zName, azName[i]) == 0)
      return 1;
  }
  return 0;
}

static sqlite3_module tg0Module = {
    /* iVersion      */ 3,
    /* xCreate       */ tg0Create,
    /* xConnect      */ tg0Connect,
    /* xBestIndex    */ tg0BestIndex,
    /* xDisconnect   */ tg0Disconnect,
    /* xDestroy      */ tg0Destroy,
    /* xOpen         */ tg0Open,
    /* xClose        */ tg0Close,
    /* xFilter       */ tg0Filter,
    /* xNext         */ tg0Next,
    /* xEof          */ tg0Eof,
    /* xColumn       */ tg0Column,
    /* xRowid        */ tg0Rowid,
    /* xUpdate       */ tg0Update,
    /* xBegin        */ 0,
    /* xSync         */ 0,
    /* xCommit       */ 0,
    /* xRollback     */ 0,
    /* xFindFunction */ tg0FindFunction,
    /* xRename       */ 0, // TODO
    /* xSavepoint    */ 0,
    /* xRelease      */ 0,
    /* xRollbackTo   */ 0,
    /* xShadowName   */ tg0ShadowName};
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
      sqlite3_mprintf(("sqlite-tg version: %s\n"
                       "sqlite-tg date: %s\n"
                       "sqlite-tg commit: %s"
                       "tg version: %s\n"
                       "tg date: %s\n"
                       "tg commit: %s"),
                      SQLITE_TG_VERSION, SQLITE_TG_DATE, SQLITE_TG_SOURCE,
                      TG_VERSION, TG_DATE, TG_COMMIT);

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

      {(char *)"tg_intersects",     2, tg_predicate_impl,   tg_geom_intersects, NULL,         DEFAULT_FLAGS},
      {(char *)"tg_disjoint",       2, tg_predicate_impl,   tg_geom_disjoint,   NULL,         DEFAULT_FLAGS},
      {(char *)"tg_contains",       2, tg_predicate_impl,   tg_geom_contains,   NULL,         DEFAULT_FLAGS},
      {(char *)"tg_within",         2, tg_predicate_impl,   tg_geom_within,     NULL,         DEFAULT_FLAGS},
      {(char *)"tg_covers",         2, tg_predicate_impl,   tg_geom_covers,     NULL,         DEFAULT_FLAGS},
      {(char *)"tg_coveredby",      2, tg_predicate_impl,   tg_geom_coveredby,  NULL,         DEFAULT_FLAGS},
      {(char *)"tg_touches",        2, tg_predicate_impl,   tg_geom_touches,    NULL,         DEFAULT_FLAGS},

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

      {(char *)"tg_multipoint",    -1, tg_multipoint, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point",          2, tg_point,      NULL,             NULL,         DEFAULT_FLAGS},
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

  rc = sqlite3_create_function(db, "tg_group_multipoint", 1,
                               SQLITE_UTF8 | SQLITE_INNOCUOUS, 0, 0,
                               tg_multipoint_step, tg_multipoint_final);
  /*
  rc = sqlite3_create_function(db, "tg_group_geometrycollection", 1,
                               SQLITE_UTF8 | SQLITE_INNOCUOUS, 0, 0,
                               tg_group_geometrycollection_step,
  tg_group_geometrycollection_final);
  */
  rc = sqlite3_create_module(db, "tg_bbox", &tg_bboxModule, NULL);
  if (rc != SQLITE_OK)
    return rc;
  rc = sqlite3_create_function_v2(db, "tg_debug", 0, DEFAULT_FLAGS,
                                  (void *)debug, tg_debug, 0, 0, sqlite3_free);
  if (rc != SQLITE_OK)
    return rc;

  rc = sqlite3_create_module_v2(db, "tg0", &tg0Module, 0, 0);
  if (rc != SQLITE_OK) {
    return rc;
  }

  return rc;
}

#pragma endregion

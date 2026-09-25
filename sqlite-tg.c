#include "sqlite3ext.h"

SQLITE_EXTENSION_INIT1

#include "tg.h"
#include "sqlite-tg.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// https://github.com/sqlite/sqlite/blob/2d3c5385bf168c85875c010bbaa79c6712eab214/src/json.c#L125-L126
#define JSON_SUBTYPE 74

#pragma region value

static const char *TG_GEOM_POINTER_NAME = "tg0-tg_geom";

#define INVALID_GEO_INPUT                                                      \
  "invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON "  \
  "(as text)."


// Ownership model: tg geometries are reference-counted (tg_*_clone increments,
// tg_*_free decrements; new objects start at zero, so their first free
// destroys them). geomValue() always returns an owned reference — parsed
// inputs are fresh, pointer inputs are cloned — so every caller must
// tg_geom_free() the result exactly once on every path, and must
// sqlite3_free(errmsg) on error. resultGeomPointer() transfers ownership to
// the SQLite pointer value (its destructor frees), so a geometry handed to it
// must NOT also be freed by the caller — hold your own clone if you need one.
int geomValue(sqlite3_value *value, struct tg_geom ** out_geom, char ** errmsg) {
  struct tg_geom * g;

  if(
    (sqlite3_value_subtype(value) == JSON_SUBTYPE)
    || ((sqlite3_value_bytes(value) > 0) && ((char *)sqlite3_value_blob(value))[0] == '{')
  ) {
    const char *text = (const char *)sqlite3_value_text(value);
    int n = sqlite3_value_bytes(value);
    g =  tg_parse_geojsonn_ix(text, n, TG_NONE);
  }else {
  switch (sqlite3_value_type(value)) {
    case SQLITE_BLOB: {
      const void * b = sqlite3_value_blob(value);
      int n = sqlite3_value_bytes(value);
      g = tg_parse_wkb_ix(b, n, TG_NONE);
      break;
    }
    case SQLITE_TEXT: {
      const char *text = (const char *)sqlite3_value_text(value);
      int n = sqlite3_value_bytes(value);
      g = tg_parse_wktn_ix(text, n, TG_NONE);
      break;
    }
    case SQLITE_NULL: {
      void *p = sqlite3_value_pointer(value, TG_GEOM_POINTER_NAME);
      if (p) {
        g = tg_geom_clone((struct tg_geom *) p);
        break;
      }
      *errmsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
      return SQLITE_ERROR;
    }
    default:
      *errmsg = sqlite3_mprintf("%s", INVALID_GEO_INPUT);
      return SQLITE_ERROR;
    }
  }

  if(tg_geom_error(g)) {
    *errmsg = sqlite3_mprintf("%s", tg_geom_error(g));
    tg_geom_free(g);
    return SQLITE_ERROR;
  }
  *out_geom = g;
  return SQLITE_OK;
}

#pragma endregion

#pragma region resulting

static void destroy_geom(void *p) {
  tg_geom_free(p);
}

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
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if(rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  resultGeomWkt(context, geom);
  tg_geom_free(geom);
}

static void tg_to_wkb(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  resultGeomWkb(context, geom);
  tg_geom_free(geom);
}

static void tg_to_geojson(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  resultGeomGeojson(context, geom);
  tg_geom_free(geom);
}

#pragma endregion

#pragma region predicates

typedef bool (*GeomPredicateFunc)(const struct tg_geom *a,
                                  const struct tg_geom *b);

static void tg_predicate_impl(sqlite3_context *context, int argc,
                              sqlite3_value **argv) {
  struct tg_geom *a = NULL;
  struct tg_geom *b = NULL;
  char * errmsg;
  int rc;

  rc = geomValue(argv[0], &a, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    goto cleanup;
  }

  rc = geomValue(argv[1], &b, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    goto cleanup;
  }

  GeomPredicateFunc func = sqlite3_user_data(context);
  sqlite3_result_int(context, func(a, b));

  cleanup:
  tg_geom_free(a);
  tg_geom_free(b);
}

#pragma endregion

#pragma region validators

static void tg_valid_geojson(sqlite3_context *context, int argc,
                             sqlite3_value **argv) {
  // TODO it's text/blob?
  const char *s = (const char *)sqlite3_value_text(argv[0]);
  int n = sqlite3_value_bytes(argv[0]);
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
  const char *s = (const char *)sqlite3_value_text(argv[0]);
  int n = sqlite3_value_bytes(argv[0]);
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
  const void *b = sqlite3_value_blob(argv[0]);
  int n = sqlite3_value_bytes(argv[0]);
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
    if (sqlite3_stricmp("none", value) == 0)
      index = TG_NONE;
    else if (sqlite3_stricmp("natural", value) == 0)
      index = TG_NATURAL;
    else if (sqlite3_stricmp("ystripes", value) == 0)
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
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

    sqlite3_result_text(context, tg_geom_type_string(tg_geom_typeof(geom)), -1,
                        SQLITE_STATIC);

  tg_geom_free(geom);
}

static void tg_extra_json(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
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

  tg_geom_free(geom);
}

static void tg_dims(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_dims(geom));

  tg_geom_free(geom);
}

static void tg_is_empty(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_is_empty(geom));

  tg_geom_free(geom);
}

static void tg_is_feature(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_is_feature(geom));

  tg_geom_free(geom);
}

static void tg_is_featurecollection(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_is_featurecollection(geom));

  tg_geom_free(geom);
}

static void tg_memsize(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int64(context, (sqlite3_int64) tg_geom_memsize(geom));

  tg_geom_free(geom);
}
#pragma endregion

#pragma region measurements

static void tg_x(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  if (tg_geom_typeof(geom) != TG_POINT) {
    const char *zErr = sqlite3_mprintf(
        "tg_x() expected a Point geometry, found %s",
        tg_geom_type_string(tg_geom_typeof(geom)));
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free((void *)zErr);
    tg_geom_free(geom);
    return;
  }

  struct tg_point point = tg_geom_point(geom);
  sqlite3_result_double(context, point.x);

  tg_geom_free(geom);
}

static void tg_y(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  if (tg_geom_typeof(geom) != TG_POINT) {
    const char *zErr = sqlite3_mprintf(
        "tg_y() expected a Point geometry, found %s",
        tg_geom_type_string(tg_geom_typeof(geom)));
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free((void *)zErr);
    tg_geom_free(geom);
    return;
  }

  struct tg_point point = tg_geom_point(geom);
  sqlite3_result_double(context, point.y);

  tg_geom_free(geom);
}

static void tg_has_z(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_has_z(geom));

  tg_geom_free(geom);
}

static void tg_has_m(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  sqlite3_result_int(context, tg_geom_has_m(geom));

  tg_geom_free(geom);
}

// tg_z()/tg_m() require a Point-typed input, same as tg_x()/tg_y(). Unlike
// tg_geom_z()/tg_geom_m() (which return 0 for a 2D point, indistinguishable
// from a real Z/M value of 0), these return SQL NULL when the point has no
// Z/M coordinate — better ergonomics than a silent 0.
static void tg_z(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  if (tg_geom_typeof(geom) != TG_POINT) {
    const char *zErr = sqlite3_mprintf(
        "tg_z() expected a Point geometry, found %s",
        tg_geom_type_string(tg_geom_typeof(geom)));
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free((void *)zErr);
    tg_geom_free(geom);
    return;
  }

  if (tg_geom_has_z(geom)) {
    sqlite3_result_double(context, tg_geom_z(geom));
  } else {
    sqlite3_result_null(context);
  }

  tg_geom_free(geom);
}

static void tg_m(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  if (tg_geom_typeof(geom) != TG_POINT) {
    const char *zErr = sqlite3_mprintf(
        "tg_m() expected a Point geometry, found %s",
        tg_geom_type_string(tg_geom_typeof(geom)));
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free((void *)zErr);
    tg_geom_free(geom);
    return;
  }

  if (tg_geom_has_m(geom)) {
    sqlite3_result_double(context, tg_geom_m(geom));
  } else {
    sqlite3_result_null(context);
  }

  tg_geom_free(geom);
}

static void tg_min_x(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  struct tg_rect rect = tg_geom_rect(geom);
  sqlite3_result_double(context, rect.min.x);

  tg_geom_free(geom);
}

static void tg_min_y(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  struct tg_rect rect = tg_geom_rect(geom);
  sqlite3_result_double(context, rect.min.y);

  tg_geom_free(geom);
}

static void tg_max_x(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  struct tg_rect rect = tg_geom_rect(geom);
  sqlite3_result_double(context, rect.max.x);

  tg_geom_free(geom);
}

static void tg_max_y(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  struct tg_rect rect = tg_geom_rect(geom);
  sqlite3_result_double(context, rect.max.y);

  tg_geom_free(geom);
}

#pragma endregion

#pragma region constructors

// tg_point(x, y) constructs a 2D point; tg_point(x, y, z) constructs a Z
// point (3-arg = Z, matching WKT convention, not M); tg_point(x, y, z, m)
// constructs a ZM point. M-only points can't be constructed (nobody asks
// for it — see ticket 12).
static void tg_point(sqlite3_context *context, int argc, sqlite3_value **argv) {
  static const char *names[] = {"X", "Y", "Z", "M"};
  double vals[4];
  for (int i = 0; i < argc; i++) {
    int type = sqlite3_value_type(argv[i]);
    if (type != SQLITE_INTEGER && type != SQLITE_FLOAT) {
      const char *zErr = sqlite3_mprintf(
          "point %s value must be an integer or float", names[i]);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      return;
    }
    vals[i] = sqlite3_value_double(argv[i]);
  }
  struct tg_point point = {vals[0], vals[1]};
  struct tg_geom *geom;
  if (argc == 4) {
    geom = tg_geom_new_point_zm(point, vals[2], vals[3]);
  } else if (argc == 3) {
    geom = tg_geom_new_point_z(point, vals[2]);
  } else {
    geom = tg_geom_new_point(point);
  }
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    tg_geom_free(geom);
    return;
  }
  resultGeomPointer(context, geom);
}

static void tg_poly_exterior_(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  const struct tg_poly * p = tg_geom_poly(geom);
  if(!p) {
    sqlite3_result_error(context, "argument to tg_poly_exterior() is not a polygon", -1);
    tg_geom_free(geom);
    return;
  }
  const struct tg_ring *ring = tg_poly_exterior(p);
  // "A tg_ring can always be safely upcasted to a tg_poly or tg_geom"
  // clone (rc increment) so the ring outlives its parent geom
  resultGeomPointer(context, (struct tg_geom*) tg_ring_clone(ring));
  tg_geom_free(geom);
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
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipoint() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      sqlite3_free(points);
      return;
    }

    if (tg_geom_typeof(geom) != TG_POINT) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipoint() at index %i expected a point, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      tg_geom_free(geom);
      sqlite3_free(points);
      return;
    }
    struct tg_point src = tg_geom_point(geom);
    struct tg_point *dest = &points[i];
    dest->x = src.x;
    dest->y = src.y;
    tg_geom_free(geom);
  }
  struct tg_geom *geom = tg_geom_new_multipoint(points, argc);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
  sqlite3_free(points);
}

static void tg_line(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_linestring_empty();
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
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_line() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      sqlite3_free(points);
      return;
    }

    if (tg_geom_typeof(geom) != TG_POINT) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_line() at index %i expected a point, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      tg_geom_free(geom);
      sqlite3_free(points);
      return;
    }
    struct tg_point src = tg_geom_point(geom);
    struct tg_point *dest = &points[i];
    dest->x = src.x;
    dest->y = src.y;
    tg_geom_free(geom);
  }
  struct tg_geom *geom = (struct tg_geom *) tg_line_new(points, argc);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
  sqlite3_free(points);
}

// tg_segment isn't a tg_geom in tg's model (see struct tg_segment in tg.h) so
// it can't flow through resultGeomPointer()/geomValue() unchanged. Instead it
// is represented at the SQL boundary as its equivalent 2-point LineString
// geometry: LINESTRING(ax ay, bx by). This composes for free with every
// existing predicate/serializer; a dedicated pointer subtype (with
// tg_segment_intersects_segment()-style fast paths) is deferred until a perf
// need is shown (see todos/update/11-rect-segment-constructors.md, option b).
static void tg_segment(sqlite3_context *context, int argc,
                        sqlite3_value **argv) {
  struct tg_point points[2];

  if (argc == 4) {
    static const char *names[] = {"aX", "aY", "bX", "bY"};
    double vals[4];
    for (int i = 0; i < 4; i++) {
      int type = sqlite3_value_type(argv[i]);
      if (type != SQLITE_INTEGER && type != SQLITE_FLOAT) {
        const char *zErr = sqlite3_mprintf(
            "segment %s value must be an integer or float", names[i]);
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        return;
      }
      vals[i] = sqlite3_value_double(argv[i]);
    }
    points[0].x = vals[0];
    points[0].y = vals[1];
    points[1].x = vals[2];
    points[1].y = vals[3];
  } else {
    // argc == 2: both arguments must be Point geometries, in any supported
    // format (WKT/WKB/GeoJSON/pointer) — same per-index error style as
    // tg_ring().
    for (int i = 0; i < 2; i++) {
      struct tg_geom *geom;
      char *errmsg;
      int rc = geomValue(argv[i], &geom, &errmsg);
      if (rc != SQLITE_OK) {
        const char *zErr = sqlite3_mprintf(
            "argument to tg_segment() at index %i is an invalid geometry: %z",
            i, errmsg);
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        return;
      }
      if (tg_geom_typeof(geom) != TG_POINT) {
        const char *zErr = sqlite3_mprintf(
            "argument to tg_segment() at index %i expected a point, found %s",
            i, tg_geom_type_string(tg_geom_typeof(geom)));
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        tg_geom_free(geom);
        return;
      }
      points[i] = tg_geom_point(geom);
      tg_geom_free(geom);
    }
  }

  struct tg_geom *geom = (struct tg_geom *) tg_line_new(points, 2);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
}

// tg_ring_new() does not require (or produce) a duplicated closing point: a
// ring of N points is always treated as closed for geometric purposes (area,
// segments, predicates all implicitly connect the last point back to the
// first — see num_segments() in tg.c). But WKT/WKB/GeoJSON serialization
// echoes back exactly the points supplied (write_ring_points_wkt() et al.
// iterate ring->npoints with no synthesized closing coordinate), so an
// "unclosed" input (last point != first point) round-trips to WKT text that
// itself looks unclosed, e.g. `POLYGON((0 0,10 0,10 10))` instead of
// `POLYGON((0 0,10 0,10 10,0 0))`. Both forms are geometrically identical
// inside tg. Callers who want conventional closed-ring WKT/GeoJSON text must
// repeat the first point as the last argument themselves; tg_ring() itself
// applies no such transformation.
static void tg_ring(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_ring *ring = tg_ring_new(NULL, 0);
    if (!ring) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, (struct tg_geom *) ring);
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
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_ring() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      sqlite3_free(points);
      return;
    }

    if (tg_geom_typeof(geom) != TG_POINT) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_ring() at index %i expected a point, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      tg_geom_free(geom);
      sqlite3_free(points);
      return;
    }
    struct tg_point src = tg_geom_point(geom);
    struct tg_point *dest = &points[i];
    dest->x = src.x;
    dest->y = src.y;
    tg_geom_free(geom);
  }
  struct tg_ring *ring = tg_ring_new(points, argc);
  if (!ring) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, (struct tg_geom *) ring);
  }
  sqlite3_free(points);
}

// Shared by tg_polygon()'s exterior/hole arguments: accepts a bare ring (the
// upcast pointers produced by tg_ring()/tg_poly_exterior()/tg_holes_each())
// or a plain Polygon geometry with no holes, in which case the polygon's
// exterior ring is used — consistent with ringEachCheck()'s precedent below.
// A Polygon WITH holes is rejected rather than silently used via its
// exterior, since that would silently drop the holes' data.
// Always consumes (frees) `geom`. On success returns an owned ring clone the
// caller must tg_ring_free(). On failure returns NULL and sets *errmsg to a
// sqlite3_mprintf()-allocated description (no trailing punctuation) meant to
// be embedded into a per-index caller message; the caller owns *errmsg.
static struct tg_ring *ringArgFromGeom(struct tg_geom *geom, char **errmsg) {
  enum tg_geom_type t = tg_geom_typeof(geom);
  if (t != TG_POLYGON) {
    *errmsg = sqlite3_mprintf("expected a ring, found %s",
                              tg_geom_type_string(t));
    tg_geom_free(geom);
    return NULL;
  }
  const struct tg_poly *poly = tg_geom_poly(geom);
  if (tg_poly_num_holes(poly) > 0) {
    *errmsg = sqlite3_mprintf(
        "expected a ring, found a Polygon with holes (its holes would be "
        "silently dropped)");
    tg_geom_free(geom);
    return NULL;
  }
  struct tg_ring *ring = tg_ring_clone(tg_poly_exterior(poly));
  tg_geom_free(geom);
  return ring;
}

// tg_poly_new() clones its exterior/holes arguments internally (tg_ring_clone()
// for each, see tg_poly_new() in tg.c) rather than taking ownership, so the
// rings collected below must be freed by us after the tg_poly_new() call
// regardless of success or failure.
static void tg_polygon(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_polygon_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }

  struct tg_ring **rings = sqlite3_malloc(argc * sizeof(struct tg_ring *));
  if (!rings) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memset(rings, 0, argc * sizeof(struct tg_ring *));

  for (int i = 0; i < argc; i++) {
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_polygon() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      for (int j = 0; j < i; j++) tg_ring_free(rings[j]);
      sqlite3_free(rings);
      return;
    }

    char *ringerr = NULL;
    struct tg_ring *ring = ringArgFromGeom(geom, &ringerr);
    if (!ring) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_polygon() at index %i %z", i, ringerr);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      for (int j = 0; j < i; j++) tg_ring_free(rings[j]);
      sqlite3_free(rings);
      return;
    }
    rings[i] = ring;
  }

  struct tg_poly *poly = tg_poly_new(rings[0],
                                     (const struct tg_ring *const *) &rings[1],
                                     argc - 1);
  for (int i = 0; i < argc; i++) tg_ring_free(rings[i]);
  sqlite3_free(rings);
  if (!poly) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, (struct tg_geom *) poly);
  }
}

// tg_rect isn't a tg_geom in tg's model (see struct tg_rect in tg.h) so it
// can't flow through resultGeomPointer()/geomValue() unchanged. Instead it is
// represented at the SQL boundary as its equivalent closed-ring Polygon
// geometry: POLYGON((minx miny, maxx miny, maxx maxy, minx maxy, minx miny)),
// counter-clockwise winding, with the closing point repeated explicitly
// (tg_ring_new() does not synthesize one — see comment above tg_ring()). This
// composes for free with every existing predicate/serializer; a dedicated
// pointer subtype (with tg_rect_intersects_rect()-style fast paths) is
// deferred until a perf need is shown (see
// todos/update/11-rect-segment-constructors.md, option b).
//
// No normalization: if minX > maxX (or minY > maxY), the polygon is built
// exactly as given. Neither tg_ring_new() nor tg_poly_new() validate point
// ordering, so there's nothing in tg to break — the result is just a
// geometrically "backwards" rect (e.g. self-intersecting/CW winding), the
// same "garbage in, garbage geometry out" behavior tg_ring() already has for
// arbitrary points.
static void tg_rect(sqlite3_context *context, int argc, sqlite3_value **argv) {
  double minx, miny, maxx, maxy;

  if (argc == 4) {
    static const char *names[] = {"minX", "minY", "maxX", "maxY"};
    double vals[4];
    for (int i = 0; i < 4; i++) {
      int type = sqlite3_value_type(argv[i]);
      if (type != SQLITE_INTEGER && type != SQLITE_FLOAT) {
        const char *zErr = sqlite3_mprintf(
            "rect %s value must be an integer or float", names[i]);
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        return;
      }
      vals[i] = sqlite3_value_double(argv[i]);
    }
    minx = vals[0];
    miny = vals[1];
    maxx = vals[2];
    maxy = vals[3];
  } else {
    // argc == 2: both arguments must be Point geometries, in any supported
    // format (WKT/WKB/GeoJSON/pointer) — same per-index error style as
    // tg_ring().
    struct tg_point pts[2];
    for (int i = 0; i < 2; i++) {
      struct tg_geom *geom;
      char *errmsg;
      int rc = geomValue(argv[i], &geom, &errmsg);
      if (rc != SQLITE_OK) {
        const char *zErr = sqlite3_mprintf(
            "argument to tg_rect() at index %i is an invalid geometry: %z", i,
            errmsg);
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        return;
      }
      if (tg_geom_typeof(geom) != TG_POINT) {
        const char *zErr = sqlite3_mprintf(
            "argument to tg_rect() at index %i expected a point, found %s", i,
            tg_geom_type_string(tg_geom_typeof(geom)));
        sqlite3_result_error(context, zErr, -1);
        sqlite3_free((void *)zErr);
        tg_geom_free(geom);
        return;
      }
      pts[i] = tg_geom_point(geom);
      tg_geom_free(geom);
    }
    minx = pts[0].x;
    miny = pts[0].y;
    maxx = pts[1].x;
    maxy = pts[1].y;
  }

  struct tg_point ring[] = {
      {minx, miny}, {maxx, miny}, {maxx, maxy}, {minx, maxy}, {minx, miny},
  };
  struct tg_ring *r = tg_ring_new(ring, 5);
  if (!r) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, (struct tg_geom *) r);
  }
}

// tg_geom_new_multilinestring() clones each line internally (tg_line_clone(),
// see tg.c) rather than taking ownership, so the clones collected below must
// be freed by us after the call regardless of success or failure.
static void tg_multilinestring(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_multilinestring_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }

  struct tg_line **lines = sqlite3_malloc(argc * sizeof(struct tg_line *));
  if (!lines) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memset(lines, 0, argc * sizeof(struct tg_line *));

  for (int i = 0; i < argc; i++) {
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multilinestring() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      for (int j = 0; j < i; j++) tg_line_free(lines[j]);
      sqlite3_free(lines);
      return;
    }

    if (tg_geom_typeof(geom) != TG_LINESTRING) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multilinestring() at index %i expected a LineString, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      tg_geom_free(geom);
      for (int j = 0; j < i; j++) tg_line_free(lines[j]);
      sqlite3_free(lines);
      return;
    }
    lines[i] = tg_line_clone(tg_geom_line(geom));
    tg_geom_free(geom);
  }

  struct tg_geom *geom = tg_geom_new_multilinestring(
      (const struct tg_line *const *) lines, argc);
  for (int i = 0; i < argc; i++) tg_line_free(lines[i]);
  sqlite3_free(lines);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
}

// tg_geom_new_multipolygon() clones each polygon internally (tg_poly_clone(),
// see tg.c) rather than taking ownership, so the clones collected below must
// be freed by us after the call regardless of success or failure. A bare
// ring upcast pointer (from tg_ring()/tg_poly_exterior()) reports typeof
// TG_POLYGON too (see ringEachCheck() below) and is accepted here the same
// way it's accepted elsewhere in this file.
static void tg_multipolygon(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_multipolygon_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }

  struct tg_poly **polys = sqlite3_malloc(argc * sizeof(struct tg_poly *));
  if (!polys) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memset(polys, 0, argc * sizeof(struct tg_poly *));

  for (int i = 0; i < argc; i++) {
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipolygon() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      for (int j = 0; j < i; j++) tg_poly_free(polys[j]);
      sqlite3_free(polys);
      return;
    }

    if (tg_geom_typeof(geom) != TG_POLYGON) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_multipolygon() at index %i expected a Polygon, found %s",
          i, tg_geom_type_string(tg_geom_typeof(geom)));
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      tg_geom_free(geom);
      for (int j = 0; j < i; j++) tg_poly_free(polys[j]);
      sqlite3_free(polys);
      return;
    }
    polys[i] = tg_poly_clone(tg_geom_poly(geom));
    tg_geom_free(geom);
  }

  struct tg_geom *geom = tg_geom_new_multipolygon(
      (const struct tg_poly *const *) polys, argc);
  for (int i = 0; i < argc; i++) tg_poly_free(polys[i]);
  sqlite3_free(polys);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
}

// tg_geom_new_geometrycollection() clones each geometry internally
// (tg_geom_clone(), see tg.c) rather than taking ownership, so the clones
// collected below must be freed by us after the call regardless of success
// or failure. Unlike the other multi-constructors, any geometry type
// (including nested collections) is accepted.
static void tg_geometrycollection(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  if (argc == 0) {
    struct tg_geom *geom = tg_geom_new_geometrycollection_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }

  struct tg_geom **geoms = sqlite3_malloc(argc * sizeof(struct tg_geom *));
  if (!geoms) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memset(geoms, 0, argc * sizeof(struct tg_geom *));

  for (int i = 0; i < argc; i++) {
    struct tg_geom *geom;
    char * errmsg;
    int rc = geomValue(argv[i], &geom, &errmsg);
    if (rc != SQLITE_OK) {
      const char *zErr = sqlite3_mprintf(
          "argument to tg_geometrycollection() at index %i is an invalid geometry: %z",
          i, errmsg);
      sqlite3_result_error(context, zErr, -1);
      sqlite3_free((void *)zErr);
      for (int j = 0; j < i; j++) tg_geom_free(geoms[j]);
      sqlite3_free(geoms);
      return;
    }
    geoms[i] = tg_geom_clone(geom);
    tg_geom_free(geom);
  }

  struct tg_geom *geom = tg_geom_new_geometrycollection(
      (const struct tg_geom *const *) geoms, argc);
  for (int i = 0; i < argc; i++) tg_geom_free(geoms[i]);
  sqlite3_free(geoms);
  if (!geom) {
    sqlite3_result_error_nomem(context);
  } else {
    resultGeomPointer(context, geom);
  }
}

struct Array {
  size_t element_size;
  size_t length;
  size_t capacity;
  void *z;
};

int tg_array_init(struct Array *array, size_t element_size, size_t init_capacity) {
  int sz = element_size * init_capacity;
  void *z = sqlite3_malloc(sz);
  if (!z) {
    return SQLITE_NOMEM;
  }
  memset(z, 0, sz);

  array->element_size = element_size;
  array->length = 0;
  array->capacity = init_capacity;
  array->z = z;
  return SQLITE_OK;
}

int tg_array_append(struct Array *array, const void *element) {
  if (array->length == array->capacity) {
    size_t new_capacity = array->capacity * 2 + 100;
    void *z = sqlite3_realloc64(array->z, array->element_size * new_capacity);
    if (z) {
      array->capacity = new_capacity;
      array->z = z;
    } else {
      return SQLITE_NOMEM;
    }
  }
  memcpy(&((unsigned char *)array->z)[array->length * array->element_size],
         element, array->element_size);
  array->length++;
  return SQLITE_OK;
}

void tg_array_cleanup(struct Array *array) {
  if (!array)
    return;
  array->element_size = 0;
  array->length = 0;
  array->capacity = 0;
  sqlite3_free(array->z);
  array->z = NULL;
}

#pragma region tg_group_multipoint
static void tg_group_multipoint_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_point), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
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
  tg_array_append(array, &src);
  tg_geom_free(geom);
}

static void tg_group_multipoint_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0) {
    return;
  }
  if (a->z == 0) {
    // TODO check if null?
    resultGeomPointer(context, tg_geom_new_multipoint_empty());
    return;
  }
  if (a->length) {
    struct tg_geom *geom = tg_geom_new_multipoint((struct tg_point *) a->z, a->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }
  tg_array_cleanup(a);
}

static void tg_group_bbox_step(sqlite3_context *context, int argc, sqlite3_value *argv[]) {
  int rc;
  struct tg_rect* rect;
  rect = (struct tg_rect *)sqlite3_aggregate_context(context, sizeof(*rect));
  if (!rect) {
    return;
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  struct tg_rect current = tg_geom_rect(geom);
  // See if this is the first geometry for the bbox, which would be zero'ed out
  if(!rect->min.x && !rect->min.y && !rect->max.x && !rect->max.y) {
    memcpy(rect, &current, sizeof(*rect));
  }
  else {
    struct tg_rect result = tg_rect_expand(*rect, current);
    memcpy(rect, &result, sizeof(*rect));
  }
  tg_geom_free(geom);  
}

static void tg_group_bbox_final(sqlite3_context *context) {
  int rc;
  struct tg_rect* rect;
  rect = (struct tg_rect *)sqlite3_aggregate_context(context, sizeof(*rect));
  if (rect == 0)
    return;
  struct tg_point points[] = {
    {rect->min.x, rect->min.y},
    {rect->max.x, rect->min.y},
    {rect->max.x, rect->max.y},
    {rect->min.x, rect->max.y},
    {rect->min.x, rect->min.y},
  };
  struct tg_ring* ring = tg_ring_new(points, 5);
  // rings upcast to tg_geom; the pointer destructor's tg_geom_free()
  // dispatches back to tg_ring_free()
  resultGeomPointer(context, (struct tg_geom *) ring);
}

static void tg_group_multipolygon_step(sqlite3_context *context, int argc, sqlite3_value *argv[]) {
  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_poly *), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  const struct tg_poly * p = tg_geom_poly(geom);
  if(!p) {
    sqlite3_result_error(
      context, "inputs to tg_group_multipolygon() must be Polygon geometries",
      -1);
    tg_geom_free(geom);
    return;
  }

  struct tg_poly * p2 = tg_poly_clone(p);
  tg_array_append(array, &p2);
  tg_geom_free(geom);
}

static void tg_group_multipolygon_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0)
    return;
  if (a->z == 0) {
    // TODO oom handle
    //resultGeomPointer(context, tg_geom_new_multipolygon_empty());
  }
  if (a->length) {

    struct tg_geom *geom = tg_geom_new_multipolygon((const struct tg_poly **) a->z, a->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }else {
    resultGeomPointer(context, tg_geom_new_multipolygon_empty());
  }
  for(int i = 0; i < a->length; i++) {
    tg_poly_free( ( (struct tg_poly**) a->z)[i]);
  }
  tg_array_cleanup(a);
}

static void tg_group_multilinestring_step(sqlite3_context *context, int argc, sqlite3_value *argv[]) {
  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_line *), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  const struct tg_line * l = tg_geom_line(geom);
  if(!l) {
    sqlite3_result_error(
      context, "inputs to tg_group_multilinestring() must be LineString geometries",
      -1);
    tg_geom_free(geom);
    return;
  }

  struct tg_line * l2 = tg_line_clone(l);
  tg_array_append(array, &l2);
  tg_geom_free(geom);
}

static void tg_group_multilinestring_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0)
    return;
  if (a->z == 0) {
    // TODO oom handle
    //resultGeomPointer(context, tg_geom_new_multipolygon_empty());
  }
  if (a->length) {
    struct tg_geom *geom = tg_geom_new_multilinestring((const struct tg_line **) a->z, a->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }else {
    resultGeomPointer(context, tg_geom_new_multilinestring_empty());
  }
  for(int i = 0; i < a->length; i++) {
    tg_line_free( ( (struct tg_line**) a->z)[i]);
  }
  tg_array_cleanup(a);
}

static void tg_group_geometry_collection_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_geom*), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  struct tg_geom *x = tg_geom_clone(geom);
  tg_array_append(array, &x);
  tg_geom_free(geom);
}

static void tg_group_geometry_collection_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0)
    return;
  if (a->z == 0) {
    struct tg_geom * g = tg_geom_new_geometrycollection_empty();
    if(!g) {
      sqlite3_result_error_nomem(context);
    }else {
      resultGeomPointer(context, g);
    }
  }
  else if (a->length) {
    struct tg_geom *geom = tg_geom_new_geometrycollection( (const struct tg_geom *const*) a->z, a->length);
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
  }
  for(int i = 0; i < a->length; i++) {
    tg_geom_free( ( (struct tg_geom**) a->z)[i]);
  }
  tg_array_cleanup(a);
}

#pragma region tg_group_line
// Aggregate input order is only well-defined when the query uses an explicit
// `order by` inside the aggregate call (`tg_group_line(p order by ...)`) or
// aggregates over an already-ordered subquery — mirrors the caveat already
// documented for the other aggregate constructors.
static void tg_group_line_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_point), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  if (tg_geom_typeof(geom) != TG_POINT) {
    sqlite3_result_error(
        context, "parameters to tg_group_line() must be Point geometries",
        -1);
    tg_geom_free(geom);
    return;
  }

  struct tg_point src = tg_geom_point(geom);
  tg_array_append(array, &src);
  tg_geom_free(geom);
}

static void tg_group_line_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0) {
    return;
  }
  if (a->z == 0) {
    // Zero rows: mirrors tg_group_multipoint_final()'s empty-aggregate
    // behavior (an empty geometry of the target type, not NULL).
    struct tg_geom *geom = tg_geom_new_linestring_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }
  if (a->length) {
    struct tg_line *line = tg_line_new((struct tg_point *) a->z, a->length);
    if (!line) {
      sqlite3_result_error_nomem(context);
    } else {
      // tg_line upcasts to tg_geom; the pointer destructor's tg_geom_free()
      // dispatches back to tg_line_free()
      resultGeomPointer(context, (struct tg_geom *) line);
    }
  }
  tg_array_cleanup(a);
}

#pragma region tg_group_ring
// Same ordering caveat as tg_group_line() above.
static void tg_group_ring_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_point), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  if (tg_geom_typeof(geom) != TG_POINT) {
    sqlite3_result_error(
        context, "parameters to tg_group_ring() must be Point geometries",
        -1);
    tg_geom_free(geom);
    return;
  }

  struct tg_point src = tg_geom_point(geom);
  tg_array_append(array, &src);
  tg_geom_free(geom);
}

static void tg_group_ring_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0) {
    return;
  }
  if (a->z == 0) {
    // Zero rows: mirrors tg_group_multipoint_final()'s empty-aggregate
    // behavior, using the same empty-ring shape as tg_ring() with no args.
    struct tg_ring *ring = tg_ring_new(NULL, 0);
    if (!ring) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, (struct tg_geom *) ring);
    }
    return;
  }
  if (a->length) {
    struct tg_ring *ring = tg_ring_new((struct tg_point *) a->z, a->length);
    if (!ring) {
      sqlite3_result_error_nomem(context);
    } else {
      // rings upcast to tg_geom; the pointer destructor's tg_geom_free()
      // dispatches back to tg_ring_free()
      resultGeomPointer(context, (struct tg_geom *) ring);
    }
  }
  tg_array_cleanup(a);
}

#pragma region tg_group_polygon
// 1-arg form: aggregates rings where the first row is the exterior and any
// subsequent rows are holes (deferred the 2-arg exterior+holes NOTES design,
// see ticket 07 — awkward aggregate shape, and this composes with
// tg_group_ring()). Ring arguments follow the same convention as
// tg_polygon()'s arguments: a bare ring upcast, or a hole-free Polygon
// geometry (see ringArgFromGeom()); a Polygon-with-holes argument is
// rejected rather than silently dropping its holes.
static void tg_group_polygon_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {

  int rc;
  struct Array *array;
  array = (struct Array *)sqlite3_aggregate_context(context, sizeof(*array));
  if (!array) {
    return;
  }
  if(!array->z) {
    rc = tg_array_init(array, sizeof(struct tg_ring *), 128);
    if(rc!=SQLITE_OK) {
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  char *ringerr = NULL;
  struct tg_ring *ring = ringArgFromGeom(geom, &ringerr);
  if (!ring) {
    const char *zErr = sqlite3_mprintf(
        "argument to tg_group_polygon() %z", ringerr);
    sqlite3_result_error(context, zErr, -1);
    sqlite3_free((void *)zErr);
    return;
  }

  tg_array_append(array, &ring);
}

// tg_poly_new() clones its exterior/holes arguments internally, so the
// rings collected in the array must be freed by us after the call
// regardless of success or failure (same pattern as the tg_polygon()
// scalar constructor).
static void tg_group_polygon_final(sqlite3_context *context) {
  struct Array *a;
  a = (struct Array *)sqlite3_aggregate_context(context, sizeof(*a));
  if (a == 0) {
    return;
  }
  if (a->z == 0) {
    // Zero rows: mirrors tg_group_multipoint_final()'s empty-aggregate
    // behavior, using the same empty shape as tg_polygon() with no args.
    struct tg_geom *geom = tg_geom_new_polygon_empty();
    if (!geom) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, geom);
    }
    return;
  }
  if (a->length) {
    struct tg_ring **rings = (struct tg_ring **) a->z;
    struct tg_poly *poly = tg_poly_new(rings[0],
                                       (const struct tg_ring *const *) &rings[1],
                                       a->length - 1);
    if (!poly) {
      sqlite3_result_error_nomem(context);
    } else {
      resultGeomPointer(context, (struct tg_geom *) poly);
    }
  }
  for (int i = 0; i < a->length; i++) {
    tg_ring_free(((struct tg_ring **) a->z)[i]);
  }
  tg_array_cleanup(a);
}

struct feature_collection_context {
  sqlite3_str * s;
  int n;
};
static void tg_feature_collection_step(sqlite3_context *context, int argc,
                               sqlite3_value *argv[]) {
  int rc;
  struct feature_collection_context *ctx;
  ctx = sqlite3_aggregate_context(context, sizeof(*ctx));
  if(!ctx) {
    return;
  }
  if(!ctx->s) {
    ctx->s = sqlite3_str_new(NULL);
    if(!ctx->s) {
      return;
    }
    sqlite3_str_appendall(ctx->s, "{\"type\":\"FeatureCollection\", \"features\": [");
  }
  if(ctx->n) {
    sqlite3_str_appendchar(ctx->s, 1, ',');
  }

  struct tg_geom *geom;
  char * errmsg;
  rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }

  size_t size = tg_geom_geojson(geom, NULL, 0);
  void *buffer = sqlite3_malloc(size + 1);
  if (buffer == 0) {
    sqlite3_result_error_nomem(context);
    tg_geom_free(geom);
    return;
  }
  tg_geom_geojson(geom, buffer, size + 1);

  // if geometry already has extra json ,it's already a Feature object
  if(tg_geom_extra_json(geom)) {
    sqlite3_str_appendf(ctx->s, "%.*s", size+1, buffer);
  }
  else {
    sqlite3_str_appendf(ctx->s, "{\"type\": \"Feature\", \"geometry\": %.*s, ", size+1, buffer);
    if(argc > 1 && sqlite3_value_subtype(argv[1]) == JSON_SUBTYPE) {
      const unsigned char * s = sqlite3_value_text(argv[1]);
      int n = sqlite3_value_bytes(argv[1]);
      sqlite3_str_appendf(ctx->s, "\"properties\": %.*s", n, s);
    } else {
      sqlite3_str_appendall(ctx->s, "\"properties\": {}");
    }
    sqlite3_str_appendall(ctx->s, "}");
  }
  sqlite3_free(buffer);
  ctx->n++;

  tg_geom_free(geom);


}

static void tg_feature_collection_final(sqlite3_context *context) {
  struct feature_collection_context *ctx;
  ctx = sqlite3_aggregate_context(context, sizeof(*ctx));
  if(!ctx) {
    return;
  }
  if(!ctx->s) {
    return;
  }
  sqlite3_str_appendall(ctx->s, "]}");
  int n = sqlite3_str_length(ctx->s);
  const char * z = sqlite3_str_finish(ctx->s);
  sqlite3_result_text(context, z, n, sqlite3_free);
  sqlite3_result_subtype(context, JSON_SUBTYPE);
}

#pragma endregion


#pragma endregion

#pragma region each table functions

struct control {
  // resulting column name in the table function; for controls that emit
  // more than one column (nCols > 1) leave this NULL and set zColumns
  // instead
  char *name;
  // raw comma-separated column list SQL fragment for multi-column controls
  // (e.g. "ax, ay, bx, by"); NULL means single column named `name`
  char *zColumns;
  // number of value columns; 0 means 1 (single-column mode via `name`)
  int nCols;
  // free the target
  void (*xFree)(void *p);
  // determine if the table function has completed
  int (*xEof)(void *p, sqlite3_int64 iRowid);
  // result the column value at the given rowid/column index
  void (*xResult)(sqlite3_context *context, void *p, sqlite3_int64 iRowid,
                  int iCol);
  // optional: validate the resolved target right after geomValue()
  // succeeds; return an sqlite3_mprintf'd error message (the caller frees
  // it) if the target isn't usable for this module, else NULL. Leave unset
  // (NULL) to accept anything geomValue() accepts.
  char *(*xCheck)(void *p);
};

#pragma region tg_points_each
int pointsEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_points((struct tg_geom *)p);
}
void pointsEachResult(sqlite3_context *context, void *p, sqlite3_int64 iRowid,
                      int iCol) {
  struct tg_point point = tg_geom_point_at((struct tg_geom *)p, iRowid);
  struct tg_geom *g = tg_geom_new_point(point);
  sqlite3_result_pointer(context, g, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}

void pointsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_lines_each
int linesEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_lines((struct tg_geom *)p);
}
void linesEachResult(sqlite3_context *context, void *p, sqlite3_int64 iRowid,
                     int iCol) {
  const struct tg_line *line = tg_geom_line_at((struct tg_geom *)p, iRowid);
  sqlite3_result_pointer(context, (void *)line, TG_GEOM_POINTER_NAME, NULL);
}

void linesEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_geometries_each
int geometriesEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_geometries((struct tg_geom *)p);
}
void geometriesEachResult(sqlite3_context *context, void *p,
                          sqlite3_int64 iRowid, int iCol) {
  const struct tg_geom *geom = tg_geom_geometry_at((struct tg_geom *)p, iRowid);
  sqlite3_result_pointer(context, (void *)geom, TG_GEOM_POINTER_NAME, NULL);
}

void geometriesEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_polygons_each
int polygonsEachEof(void *p, sqlite3_int64 iRowid) {
  return iRowid >= tg_geom_num_polys((struct tg_geom *)p);
}
void polygonsEachResult(sqlite3_context *context, void *p,
                        sqlite3_int64 iRowid, int iCol) {
  const struct tg_poly *poly = tg_geom_poly_at((struct tg_geom *)p, iRowid);

  // rationale: tg_poly can safely upcast to tg_geom
  // https://github.com/tidwall/tg/blob/main/docs/API.md#upcasting
  sqlite3_result_pointer(context, (void *)(void *)poly, TG_GEOM_POINTER_NAME,
                         NULL);
}

void polygonsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_holes_each
// the cursor target is always the tg_geom from geomValue(); resolve it to a
// poly here rather than casting, so non-polygon inputs yield zero rows
// instead of misreading memory
int holesEachEof(void *p, sqlite3_int64 iRowid) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  if (!poly)
    return 1;
  return iRowid >= tg_poly_num_holes(poly);
}
void holesEachResult(sqlite3_context *context, void *p,
                        sqlite3_int64 iRowid, int iCol) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  const struct tg_ring *ring = tg_poly_hole_at(poly, iRowid);

  // rationale: tg_ring can safely upcast to tg_geom
  sqlite3_result_pointer(context, (void *)(void *)ring, TG_GEOM_POINTER_NAME,
                         NULL);
}

void holesEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

// Shared validators for the tg_line_*/tg_ring_* modules below. The cursor
// target is always the tg_geom (or upcast tg_line/tg_ring) produced by
// geomValue(); tg_geom_typeof() is safe to call on any of those (the `type`
// field lives in the shared `head` struct at a fixed offset, see tg's
// upcasting docs) and, unlike resolving via tg_geom_line()/tg_geom_poly(),
// it distinguishes "wrong geometry type" from "right type but empty" (whose
// resolved line/ring pointer is legitimately NULL either way).
static char *lineEachCheck(void *p) {
  struct tg_geom *geom = (struct tg_geom *)p;
  if (tg_geom_typeof(geom) == TG_LINESTRING)
    return NULL;
  return sqlite3_mprintf(
      "expected a LineString geometry, found %s",
      tg_geom_type_string(tg_geom_typeof(geom)));
}

// Accepts either a bare ring (the upcast pointers produced by
// tg_poly_exterior()/tg_poly_hole_at(), e.g. via tg_holes_each) or a plain
// Polygon geometry, in which case the polygon's exterior ring is used.
// Both cases report as TG_POLYGON per tg's own upcasting rules (see
// series_new() in tg.c), so a single typeof() check covers them.
static char *ringEachCheck(void *p) {
  struct tg_geom *geom = (struct tg_geom *)p;
  if (tg_geom_typeof(geom) == TG_POLYGON)
    return NULL;
  return sqlite3_mprintf(
      "expected a ring (e.g. from tg_poly_exterior() or tg_holes_each()) or "
      "a Polygon geometry, found %s",
      tg_geom_type_string(tg_geom_typeof(geom)));
}

#pragma region tg_line_points_each
int linePointsEachEof(void *p, sqlite3_int64 iRowid) {
  const struct tg_line *line = tg_geom_line((struct tg_geom *)p);
  if (!line)
    return 1;
  return iRowid >= tg_line_num_points(line);
}
void linePointsEachResult(sqlite3_context *context, void *p,
                          sqlite3_int64 iRowid, int iCol) {
  const struct tg_line *line = tg_geom_line((struct tg_geom *)p);
  struct tg_point point = tg_line_point_at(line, iRowid);
  struct tg_geom *g = tg_geom_new_point(point);
  sqlite3_result_pointer(context, g, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}
void linePointsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_line_segments_each
int lineSegmentsEachEof(void *p, sqlite3_int64 iRowid) {
  const struct tg_line *line = tg_geom_line((struct tg_geom *)p);
  if (!line)
    return 1;
  return iRowid >= tg_line_num_segments(line);
}
void lineSegmentsEachResult(sqlite3_context *context, void *p,
                            sqlite3_int64 iRowid, int iCol) {
  const struct tg_line *line = tg_geom_line((struct tg_geom *)p);
  struct tg_segment seg = tg_line_segment_at(line, iRowid);
  switch (iCol) {
  case 0: sqlite3_result_double(context, seg.a.x); break;
  case 1: sqlite3_result_double(context, seg.a.y); break;
  case 2: sqlite3_result_double(context, seg.b.x); break;
  case 3: sqlite3_result_double(context, seg.b.y); break;
  }
}
void lineSegmentsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_ring_points_each
int ringPointsEachEof(void *p, sqlite3_int64 iRowid) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  const struct tg_ring *ring = poly ? tg_poly_exterior(poly) : NULL;
  if (!ring)
    return 1;
  return iRowid >= tg_ring_num_points(ring);
}
void ringPointsEachResult(sqlite3_context *context, void *p,
                          sqlite3_int64 iRowid, int iCol) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  const struct tg_ring *ring = tg_poly_exterior(poly);
  struct tg_point point = tg_ring_point_at(ring, iRowid);
  struct tg_geom *g = tg_geom_new_point(point);
  sqlite3_result_pointer(context, g, TG_GEOM_POINTER_NAME,
                         (void (*)(void *))tg_geom_free);
}
void ringPointsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
#pragma endregion

#pragma region tg_ring_segments_each
int ringSegmentsEachEof(void *p, sqlite3_int64 iRowid) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  const struct tg_ring *ring = poly ? tg_poly_exterior(poly) : NULL;
  if (!ring)
    return 1;
  return iRowid >= tg_ring_num_segments(ring);
}
void ringSegmentsEachResult(sqlite3_context *context, void *p,
                            sqlite3_int64 iRowid, int iCol) {
  const struct tg_poly *poly = tg_geom_poly((struct tg_geom *)p);
  const struct tg_ring *ring = tg_poly_exterior(poly);
  struct tg_segment seg = tg_ring_segment_at(ring, iRowid);
  switch (iCol) {
  case 0: sqlite3_result_double(context, seg.a.x); break;
  case 1: sqlite3_result_double(context, seg.a.y); break;
  case 2: sqlite3_result_double(context, seg.b.x); break;
  case 3: sqlite3_result_double(context, seg.b.y); break;
  }
}
void ringSegmentsEachFree(void *p) { tg_geom_free((struct tg_geom *)p); }
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
};

// number of value columns a control exposes; the hidden "source" column
// always immediately follows them
static int controlNumCols(struct control *pControl) {
  return pControl->nCols > 0 ? pControl->nCols : 1;
}

static int template_eachConnect(sqlite3 *db, void *pAux, int argc,
                                const char *const *argv, sqlite3_vtab **ppVtab,
                                char **pzErr) {
  template_each_vtab *pNew;
  int rc;

  struct control *pControl = (struct control *)pAux;
  const char *schema =
      pControl->zColumns
          ? sqlite3_mprintf("CREATE TABLE x(%s, source hidden)",
                            pControl->zColumns)
          : sqlite3_mprintf("CREATE TABLE x(%w, source hidden)",
                            pControl->name);
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
    pCur->target = NULL;
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int template_eachBestIndex(sqlite3_vtab *pVTab,
                                  sqlite3_index_info *pIdxInfo) {
  struct control *pControl = ((template_each_vtab *)pVTab)->pControl;
  int iSourceCol = controlNumCols(pControl);
  int hasSource = 0;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    if (pCons->iColumn == iSourceCol) {
      if (!hasSource && !pCons->usable ||
          pCons->op != SQLITE_INDEX_CONSTRAINT_EQ)
        return SQLITE_CONSTRAINT;
      hasSource = 1;
      pIdxInfo->aConstraintUsage[i].argvIndex = 1;
      pIdxInfo->aConstraintUsage[i].omit = 1;
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
  if (pCur->target) {
    pControl->xFree(pCur->target);
    pCur->target = 0;
  }
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", errmsg);
    sqlite3_free(errmsg);
    return SQLITE_ERROR;
  }
  if (pControl->xCheck) {
    char *checkErr = pControl->xCheck(geom);
    if (checkErr) {
      sqlite3_free(pVtabCursor->pVtab->zErrMsg);
      pVtabCursor->pVtab->zErrMsg = checkErr;
      pControl->xFree(geom);
      return SQLITE_ERROR;
    }
  }
  pCur->target = geom;
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
  struct control *pControl = ((template_each_vtab *)pCur->base.pVtab)->pControl;
  if (i < controlNumCols(pControl)) {
    pControl->xResult(context, pCur->target, pCur->iRowid, i);
  }
  return SQLITE_OK;
}


// overwrite `->>` on `tg_each()` to extract JSON path from `tg_geom_extra_json()`
static void tg_each_arrow(sqlite3_context *context, int argc,
                              sqlite3_value **argv) {
  int rc;
  sqlite3_stmt * stmt = NULL;
  struct tg_geom *g = NULL;
  char * errmsg = NULL;

  rc = geomValue(argv[0], &g, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    goto cleanup;
  }

  const char * extra_json = tg_geom_extra_json(g);
  if(!extra_json) {
    goto cleanup;
  }

  rc = sqlite3_prepare_v2(sqlite3_context_db_handle(context), "select ?1 ->> ?2", -1, &stmt, NULL);
  if(rc != SQLITE_OK) {
    sqlite3_result_error(context, "error preparing", -1);
    goto cleanup;
  }
  sqlite3_bind_text(stmt, 1, extra_json, -1, SQLITE_TRANSIENT);
  sqlite3_bind_value(stmt, 2, argv[1]);
  rc = sqlite3_step(stmt);
  if(rc != SQLITE_ROW) {
    sqlite3_result_error(context, sqlite3_errmsg(sqlite3_context_db_handle(context)), -1);
    goto cleanup;
  }
  sqlite3_result_value(context, sqlite3_column_value(stmt, 0));

  cleanup:
    tg_geom_free(g);
    sqlite3_finalize(stmt);
}
static int template_eachFindFunction(sqlite3_vtab *pVtab, int nArg, const char *zName,
                           void (**pxFunc)(sqlite3_context *, int,
                                           sqlite3_value **),
                           void **ppArg) {
  if (sqlite3_stricmp(zName, "->>") == 0 && nArg == 2) {
    *pxFunc = tg_each_arrow;
    return 1;
  }
  return 0;
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
    /* xFindMethod */ template_eachFindFunction,
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
  struct tg_geom *geom;
  char * errmsg;
  int rc = geomValue(argv[0], &geom, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", errmsg);
    sqlite3_free(errmsg);
    return SQLITE_ERROR;
  }

  pCur->rect = tg_geom_rect(geom);
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

#pragma region distance

// ---------------------------------------------------------------------------
// Distance kernel (plans/knn/plan.md decisions 6, 7, 9).
//
// tg is strictly planar and has no geometry-to-geometry distance, so every
// distance number sqlite-tg produces is computed here. Two metrics:
//
//   TG_METRIC_GEODESIC: inputs are lon/lat degrees, outputs meters — the
//     great-circle (haversine) distance on a sphere of radius
//     TG_EARTH_RADIUS_METERS. Exact for point-to-point. For a segment, the
//     closest point is found on tg's planar lon/lat edge in a local frame
//     scaled by cos(lat_query), then the haversine to it — an approximation
//     whose error is second-order in edge length. Edges stay planar because
//     that is what every tg predicate and both bbox indexes assume;
//     great-circle edges would bulge outside their vertices' bbox and
//     disagree with tg's point-in-polygon.
//   TG_METRIC_PLANAR: Euclidean in coordinate units.
//
// Rect distances are *lower bounds* that drive best-first traversals (the
// tg0 rtree walk and tg's own tg_ring/line_nearest_segment): for every point
// X inside rect, point_rect(q, rect) <= point_point(q, X). The geodesic bound
// is the S2LatLngRect::GetDistance construction and is wrap-aware in
// longitude, so a query at lon 179.9 is near a rect at lon -179.9 even though
// rtree bboxes themselves never wrap.
// ---------------------------------------------------------------------------

typedef enum {
  TG_METRIC_GEODESIC = 0,
  TG_METRIC_PLANAR = 1,
} tg_metric;

// IUGG mean Earth radius, the constant turf.js and PostGIS's spherical
// functions use
#define TG_EARTH_RADIUS_METERS 6371008.8
#define TG_DEG_TO_RAD (M_PI / 180.0)

// 1 when (x, y) is a finite lon/lat coordinate pair.
static int tg_lonlat_valid(double x, double y) {
  return isfinite(x) && isfinite(y) && fabs(x) <= 180.0 && fabs(y) <= 90.0;
}

// The parameter t in [0, 1] of p's closest point on segment s in the plane:
// clamped projection, 0 for a degenerate (or NaN) segment.
static double tg_segment_project(struct tg_point p, struct tg_segment s) {
  double c = s.b.x - s.a.x;
  double d = s.b.y - s.a.y;
  double e = c * c + d * d;
  if (!(e > 0)) {
    return 0.0;
  }
  double t = ((p.x - s.a.x) * c + (p.y - s.a.y) * d) / e;
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

// The point at parameter t along segment s.
static struct tg_point tg_segment_lerp(struct tg_segment s, double t) {
  return (struct tg_point){s.a.x + t * (s.b.x - s.a.x),
                           s.a.y + t * (s.b.y - s.a.y)};
}

static double tg_planar_point_point(struct tg_point a, struct tg_point b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return sqrt(dx * dx + dy * dy);
}

static double tg_planar_point_segment(struct tg_point p, struct tg_segment s) {
  double t = tg_segment_project(p, s);
  struct tg_point c = {s.a.x + t * (s.b.x - s.a.x), s.a.y + t * (s.b.y - s.a.y)};
  return tg_planar_point_point(p, c);
}

static double tg_planar_point_rect(struct tg_point p, struct tg_rect r) {
  double dx = fmax(fmax(r.min.x - p.x, p.x - r.max.x), 0.0);
  double dy = fmax(fmax(r.min.y - p.y, p.y - r.max.y), 0.0);
  return sqrt(dx * dx + dy * dy);
}

// Great-circle distance in meters between two lon/lat points. The
// sin^2(dlon/2) term is 2*pi-periodic, so the antimeridian needs no special
// handling.
static double tg_haversine(struct tg_point a, struct tg_point b) {
  double lat1 = a.y * TG_DEG_TO_RAD;
  double lat2 = b.y * TG_DEG_TO_RAD;
  double sinLat = sin((lat2 - lat1) / 2);
  double sinLon = sin((b.x - a.x) * TG_DEG_TO_RAD / 2);
  double h = sinLat * sinLat + cos(lat1) * cos(lat2) * sinLon * sinLon;
  h = h < 0 ? 0 : (h > 1 ? 1 : h);
  return 2 * TG_EARTH_RADIUS_METERS * asin(sqrt(h));
}

// Great-circle distance from q to the nearest point of planar lon/lat
// segment s. The nearest point is found by golden-section search of the
// haversine along the edge (unimodal for any edge that does not wrap around
// the sphere), with both endpoints checked explicitly so a constrained
// minimum is exact. The result is always the distance to a point *on* the
// edge, so a rect bound over the edge's bbox remains a valid lower bound.
static double tg_geodesic_point_segment(struct tg_point q,
                                        struct tg_segment s) {
  double best = fmin(tg_haversine(q, s.a), tg_haversine(q, s.b));
  if (s.a.x == s.b.x && s.a.y == s.b.y) {
    return best;
  }
  const double invphi = 0.6180339887498949;
  double lo = 0.0;
  double hi = 1.0;
  double c = hi - invphi * (hi - lo);
  double d = lo + invphi * (hi - lo);
  double fc = tg_haversine(q, tg_segment_lerp(s, c));
  double fd = tg_haversine(q, tg_segment_lerp(s, d));
  // 40 halvings shrink the bracket below 5e-9 of the edge length
  for (int i = 0; i < 40; i++) {
    if (fc < fd) {
      hi = d;
      d = c;
      fd = fc;
      c = hi - invphi * (hi - lo);
      fc = tg_haversine(q, tg_segment_lerp(s, c));
    } else {
      lo = c;
      c = d;
      fc = fd;
      d = lo + invphi * (hi - lo);
      fd = tg_haversine(q, tg_segment_lerp(s, d));
    }
  }
  return fmin(best, fmin(fc, fd));
}

static void tg_lonlat_to_xyz(double lon, double lat, double v[3]) {
  double la = lat * TG_DEG_TO_RAD;
  double lo = lon * TG_DEG_TO_RAD;
  v[0] = cos(la) * cos(lo);
  v[1] = cos(la) * sin(lo);
  v[2] = sin(la);
}

static void tg_xyz_cross(const double a[3], const double b[3], double out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static double tg_xyz_dot(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double tg_xyz_norm(const double a[3]) { return sqrt(tg_xyz_dot(a, a)); }

// Angle in radians between two unit vectors; atan2 form stays accurate for
// tiny angles where acos(dot) would not.
static double tg_xyz_angle(const double a[3], const double b[3]) {
  double c[3];
  tg_xyz_cross(a, b, c);
  return atan2(tg_xyz_norm(c), tg_xyz_dot(a, b));
}

// Angular distance from unit vector q to the great-circle arc a..b (shorter
// than a half circle). When q's perpendicular foot lies inside the arc the
// distance is to the great circle itself, otherwise to the nearer endpoint.
static double tg_xyz_point_arc(const double q[3], const double a[3],
                               const double b[3]) {
  double n[3];
  tg_xyz_cross(a, b, n);
  double nn = tg_xyz_norm(n);
  if (nn < 1e-15) {
    // a == b: a degenerate arc
    return tg_xyz_angle(q, a);
  }
  // tangents at a (toward b) and at b (toward a): q projects inside the arc
  // when it is on the far side of both endpoint planes
  double ta[3], tb[3];
  tg_xyz_cross(n, a, ta);
  tg_xyz_cross(b, n, tb);
  if (tg_xyz_dot(q, ta) >= 0 && tg_xyz_dot(q, tb) >= 0) {
    double d = tg_xyz_dot(q, n) / nn;
    d = d < -1 ? -1 : (d > 1 ? 1 : d);
    return asin(fabs(d));
  }
  return fmin(tg_xyz_angle(q, a), tg_xyz_angle(q, b));
}

// Lower bound, in meters, of the great-circle distance from lon/lat point q
// to any point of the lon/lat rectangle r (S2LatLngRect::GetDistance):
//
// - q inside r: 0.
// - q's longitude within r's longitude span: the nearest point is straight
//   north or south along q's meridian, on the nearer parallel edge — exact,
//   because for a line of constant latitude the closest point is at the
//   query's own longitude (cos d is monotone in |dlon|).
// - otherwise: the nearest point lies on the meridian edge nearer to q in
//   (wrapped) longitude, but generally not at q's latitude — meridians are
//   great circles and the perpendicular foot slides along them — so it is a
//   3-D point-to-arc computation. Clamp-then-haversine would be an *upper*
//   bound here and would silently break best-first ordering.
static double tg_geodesic_point_rect(struct tg_point q, struct tg_rect r) {
  int lonWithin =
      (q.x >= r.min.x && q.x <= r.max.x) ||
      (q.x + 360 >= r.min.x && q.x + 360 <= r.max.x) ||
      (q.x - 360 >= r.min.x && q.x - 360 <= r.max.x);
  if (lonWithin) {
    if (q.y >= r.min.y && q.y <= r.max.y) {
      return 0.0;
    }
    double dlat = q.y < r.min.y ? r.min.y - q.y : q.y - r.max.y;
    return dlat * TG_DEG_TO_RAD * TG_EARTH_RADIUS_METERS;
  }
  double dlo = fabs(remainder(q.x - r.min.x, 360.0));
  double dhi = fabs(remainder(q.x - r.max.x, 360.0));
  double edge = dlo <= dhi ? r.min.x : r.max.x;
  double qv[3], av[3], bv[3];
  tg_lonlat_to_xyz(q.x, q.y, qv);
  tg_lonlat_to_xyz(edge, r.min.y, av);
  tg_lonlat_to_xyz(edge, r.max.y, bv);
  return tg_xyz_point_arc(qv, av, bv) * TG_EARTH_RADIUS_METERS;
}

static double tg_metric_point_point(tg_metric metric, struct tg_point a,
                                    struct tg_point b) {
  return metric == TG_METRIC_PLANAR ? tg_planar_point_point(a, b)
                                    : tg_haversine(a, b);
}

static double tg_metric_point_segment(tg_metric metric, struct tg_point q,
                                      struct tg_segment s) {
  return metric == TG_METRIC_PLANAR ? tg_planar_point_segment(q, s)
                                    : tg_geodesic_point_segment(q, s);
}

// Lower bound of the distance from q to any point inside r.
static double tg_metric_point_rect(tg_metric metric, struct tg_point q,
                                   struct tg_rect r) {
  return metric == TG_METRIC_PLANAR ? tg_planar_point_rect(q, r)
                                    : tg_geodesic_point_rect(q, r);
}

// udata for tg_ring_nearest_segment / tg_line_nearest_segment: the metric,
// the query point, and the distance of the first (nearest) segment iterated.
struct tg_metric_nearest {
  tg_metric metric;
  struct tg_point q;
  double dist;
};

static double tg_metric_nearest_rect_dist(struct tg_rect rect, int *more,
                                          void *udata) {
  struct tg_metric_nearest *ctx = udata;
  (void)more;
  return tg_metric_point_rect(ctx->metric, ctx->q, rect);
}

static double tg_metric_nearest_seg_dist(struct tg_segment seg, int *more,
                                         void *udata) {
  struct tg_metric_nearest *ctx = udata;
  (void)more;
  return tg_metric_point_segment(ctx->metric, ctx->q, seg);
}

// tg iterates segments nearest-first, so the first callback is the answer
static bool tg_metric_nearest_iter(struct tg_segment seg, double dist,
                                   int index, void *udata) {
  struct tg_metric_nearest *ctx = udata;
  (void)seg;
  (void)index;
  ctx->dist = dist;
  return false;
}

// Distance from q to the nearest segment of ring (INFINITY when the ring
// has none). Returns false on tg OOM.
static bool tg_metric_point_ring(tg_metric metric, struct tg_point q,
                                 const struct tg_ring *ring, double *out) {
  struct tg_metric_nearest ctx = {metric, q, INFINITY};
  if (!tg_ring_nearest_segment(ring, tg_metric_nearest_rect_dist,
                               tg_metric_nearest_seg_dist,
                               tg_metric_nearest_iter, &ctx)) {
    return false;
  }
  *out = ctx.dist;
  return true;
}

static bool tg_metric_point_line(tg_metric metric, struct tg_point q,
                                 const struct tg_line *line, double *out) {
  struct tg_metric_nearest ctx = {metric, q, INFINITY};
  if (!tg_line_nearest_segment(line, tg_metric_nearest_rect_dist,
                               tg_metric_nearest_seg_dist,
                               tg_metric_nearest_iter, &ctx)) {
    return false;
  }
  *out = ctx.dist;
  return true;
}

// 0 when the polygon contains q (boundary included), else the distance to
// the nearest edge of the exterior or any hole.
static bool tg_metric_point_poly(tg_metric metric, struct tg_point q,
                                 const struct tg_poly *poly, double *out) {
  if (tg_geom_intersects_xy((const struct tg_geom *)poly, q.x, q.y)) {
    *out = 0.0;
    return true;
  }
  double best = INFINITY;
  double d;
  if (!tg_metric_point_ring(metric, q, tg_poly_exterior(poly), &d)) {
    return false;
  }
  best = fmin(best, d);
  for (int i = 0; i < tg_poly_num_holes(poly); i++) {
    if (!tg_metric_point_ring(metric, q, tg_poly_hole_at(poly, i), &d)) {
      return false;
    }
    best = fmin(best, d);
  }
  *out = best;
  return true;
}

// Distance from q to the nearest point of geom under metric, for all 7
// geometry types; INFINITY for empty geometries. Returns false on tg OOM
// (the segment traversals may allocate).
static bool tg_metric_point_geom(tg_metric metric, struct tg_point q,
                                 const struct tg_geom *geom, double *out) {
  double best = INFINITY;
  double d;
  switch (tg_geom_typeof(geom)) {
  case TG_POINT:
    if (!tg_geom_is_empty(geom)) {
      best = tg_metric_point_point(metric, q, tg_geom_point(geom));
    }
    break;
  case TG_LINESTRING: {
    const struct tg_line *line = tg_geom_line(geom);
    if (line) {
      if (!tg_metric_point_line(metric, q, line, &best)) {
        return false;
      }
    }
    break;
  }
  case TG_POLYGON: {
    const struct tg_poly *poly = tg_geom_poly(geom);
    if (poly) {
      if (!tg_metric_point_poly(metric, q, poly, &best)) {
        return false;
      }
    }
    break;
  }
  case TG_MULTIPOINT:
    for (int i = 0; i < tg_geom_num_points(geom); i++) {
      best = fmin(best,
                  tg_metric_point_point(metric, q, tg_geom_point_at(geom, i)));
    }
    break;
  case TG_MULTILINESTRING:
    for (int i = 0; i < tg_geom_num_lines(geom); i++) {
      if (!tg_metric_point_line(metric, q, tg_geom_line_at(geom, i), &d)) {
        return false;
      }
      best = fmin(best, d);
    }
    break;
  case TG_MULTIPOLYGON:
    for (int i = 0; i < tg_geom_num_polys(geom); i++) {
      if (!tg_metric_point_poly(metric, q, tg_geom_poly_at(geom, i), &d)) {
        return false;
      }
      best = fmin(best, d);
    }
    break;
  case TG_GEOMETRYCOLLECTION:
    for (int i = 0; i < tg_geom_num_geometries(geom); i++) {
      if (!tg_metric_point_geom(metric, q, tg_geom_geometry_at(geom, i), &d)) {
        return false;
      }
      best = fmin(best, d);
    }
    break;
  }
  *out = best;
  return true;
}

// tg_distance_sphere(a, b): great-circle distance in meters between a Point
// and any geometry (the nearest point of it; 0 when a polygon contains the
// point). At least one argument must be a Point — tg has no
// geometry-to-geometry distance. NULL for empty geometries. Coordinates
// must be lon/lat.
static void tg_distance_sphere(sqlite3_context *context, int argc,
                               sqlite3_value **argv) {
  struct tg_geom *a = NULL;
  struct tg_geom *b = NULL;
  char *errmsg = NULL;
  (void)argc;
  if (geomValue(argv[0], &a, &errmsg) != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  if (geomValue(argv[1], &b, &errmsg) != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    tg_geom_free(a);
    return;
  }
  const struct tg_geom *point = NULL;
  const struct tg_geom *other = NULL;
  if (tg_geom_typeof(a) == TG_POINT) {
    point = a;
    other = b;
  } else if (tg_geom_typeof(b) == TG_POINT) {
    point = b;
    other = a;
  } else {
    char *msg = sqlite3_mprintf(
        "tg_distance_sphere() expects at least one Point argument, found %s "
        "and %s",
        tg_geom_type_string(tg_geom_typeof(a)),
        tg_geom_type_string(tg_geom_typeof(b)));
    sqlite3_result_error(context, msg, -1);
    sqlite3_free(msg);
    goto done;
  }
  if (tg_geom_is_empty(point) || tg_geom_is_empty(other)) {
    sqlite3_result_null(context);
    goto done;
  }
  struct tg_point q = tg_geom_point(point);
  struct tg_rect r = tg_geom_rect(other);
  if (!tg_lonlat_valid(q.x, q.y) || !tg_lonlat_valid(r.min.x, r.min.y) ||
      !tg_lonlat_valid(r.max.x, r.max.y)) {
    sqlite3_result_error(context,
                         "tg_distance_sphere() expects lon/lat coordinates "
                         "(-180 <= x <= 180, -90 <= y <= 90)",
                         -1);
    goto done;
  }
  double d;
  if (!tg_metric_point_geom(TG_METRIC_GEODESIC, q, other, &d)) {
    sqlite3_result_error_nomem(context);
    goto done;
  }
  if (isinf(d)) {
    sqlite3_result_null(context);
  } else {
    sqlite3_result_double(context, d);
  }
done:
  tg_geom_free(a);
  tg_geom_free(b);
}

#pragma endregion

#pragma region tg0 virtual table

#define TG0_MAX_GEOMETRY_COLUMNS 8
#define TG0_MAX_DATA_COLUMNS 16
#define TG0_MAX_USER_COLUMNS (TG0_MAX_GEOMETRY_COLUMNS + TG0_MAX_DATA_COLUMNS)

// clang-format off
// The different overloaded and intercepted functions for tg0() tables.
#define TG0_FUNC_INTERSECTS SQLITE_INDEX_CONSTRAINT_FUNCTION
#define TG0_FUNC_DISJOINT   SQLITE_INDEX_CONSTRAINT_FUNCTION + 1
#define TG0_FUNC_CONTAINS   SQLITE_INDEX_CONSTRAINT_FUNCTION + 2
#define TG0_FUNC_WITHIN     SQLITE_INDEX_CONSTRAINT_FUNCTION + 3
#define TG0_FUNC_COVERS     SQLITE_INDEX_CONSTRAINT_FUNCTION + 4
#define TG0_FUNC_COVEREDBY  SQLITE_INDEX_CONSTRAINT_FUNCTION + 5
#define TG0_FUNC_TOUCHES    SQLITE_INDEX_CONSTRAINT_FUNCTION + 6
#define TG0_FUNC_EQUALS     SQLITE_INDEX_CONSTRAINT_FUNCTION + 7
// clang-format on

// ---------------------------------------------------------------------------
// tg0 idxStr encoding (plan.md "Query plans / idxStr encoding")
//
// idxNum is unused. idxStr is a header character naming the plan, followed by
// one fixed-width 4-character block per consumed argv slot:
//
//   idxStr := header block*
//   header := '1'   -- fullscan: iterate t_content directly
//           | '2'   -- point: rowid EQ / text-pk id EQ, argv[0] = the key
//           | '3'   -- predicate: rtree prefilter + exact tg predicate,
//                      argv[0] = the query geometry
//           | '4'   -- KNN: best-first rtree walk from a query point,
//                      argv[0] = the MATCH value (a Point geometry)
//   block  := 'K' [geom-col] [metric] '_'          -- KNN block
//           | 'P' [geom-col] [pred-op] [strategy]   -- predicate block
//           | 'D' [cmp-op] '_' '_'                  -- distance bound block
//           | '&' [data-col] [cmp-op] '_'           -- pushdown block
//
// Blocks appear in argv order: the k'th block describes argv[k]. A predicate
// plan carries exactly one 'P' block, always first: [geom-col] is 'A' + the
// geometry column's index into geometry_columns[], [pred-op] identifies the
// tg predicate, and [strategy] the rtree prefilter (see
// tg0_predicate_plans[] for both alphabets).
//
// A KNN plan carries exactly one 'K' block, always first: [geom-col] as
// above and [metric] 'G' (geodesic) or 'P' (planar), informational — the
// column's metric= option is authoritative. It may then carry (in this
// order) one 'P' block — a tg predicate on the *same* geometry column,
// folded into the walk as a cell prune + exact per-candidate test — and one
// 'D' block — `distance < ?` ('d') or `distance <= ?` ('c'), the walk stops
// once every remaining bound exceeds it (omit stays 0, SQLite re-checks the
// exact value) — before its '&' pushdown blocks, which prefilter the
// per-rowid content fetch. See "tg0 KNN plan" below.
//
// Predicate and fullscan plans may then carry up to TG0_MAX_PUSHDOWN_BLOCKS
// '&' pushdown blocks — EQ/NE/LT/LE/GT/GE constraints on plain data columns
// folded into the content scan's WHERE clause so cheap B-tree compares run
// before WKB parse + exact predicate (plan.md decision 10). Pure
// optimization: their omit flag stays 0, so SQLite re-checks every pushed
// constraint and correctness never depends on the prefilter. [data-col] is
// 'A' + the column's index into data_columns[] and [cmp-op] is the vec0
// metadata operator alphabet ('a'= 'b'> 'c'<= 'd'< 'e'>= 'f'!=,
// sqlite-vec's vec0_metadata_operator).
// ---------------------------------------------------------------------------

#define TG0_PLAN_FULLSCAN '1'
#define TG0_PLAN_POINT '2'
#define TG0_PLAN_PREDICATE '3'
#define TG0_PLAN_KNN '4'

#define TG0_IDXSTR_KIND_KNN 'K'
#define TG0_IDXSTR_KIND_PREDICATE 'P'
#define TG0_IDXSTR_KIND_DISTANCE 'D'
#define TG0_IDXSTR_KIND_PUSHDOWN '&'

// pushdown block [cmp-op] alphabet, mirroring vec0's vec0_metadata_operator
#define TG0_PUSHDOWN_OP_EQ 'a'
#define TG0_PUSHDOWN_OP_GT 'b'
#define TG0_PUSHDOWN_OP_LE 'c'
#define TG0_PUSHDOWN_OP_LT 'd'
#define TG0_PUSHDOWN_OP_GE 'e'
#define TG0_PUSHDOWN_OP_NE 'f'

// cap on consumed '&' pushdown blocks per plan, bounding idxStr size. Extra
// data-column constraints stay unconsumed; SQLite post-filters them
#define TG0_MAX_PUSHDOWN_BLOCKS 8

// rtree prefilter strategies for predicate plans
#define TG0_STRATEGY_OVERLAP 'O'  // stored bbox overlaps the query bbox
#define TG0_STRATEGY_CONTAINS 'C' // stored bbox contains the query bbox
#define TG0_STRATEGY_WITHIN 'W'   // stored bbox within the query bbox
#define TG0_STRATEGY_FULLSCAN 'F' // no prefilter (disjoint): exact test only

// ---------------------------------------------------------------------------
// tg0 constructor parser
//
// Hand-rolled tokenizer + parsers for the arguments of
// `CREATE VIRTUAL TABLE ... USING tg0(...)`. Grammar:
//
//   column-def :=
//       name INTEGER PRIMARY KEY
//     | name TEXT PRIMARY KEY
//     | name geom-type [geom-option]*
//     | name (TEXT | INT | INTEGER | FLOAT | DOUBLE | BLOB)
//
//   geom-option := INDEXING "=" (NONE | NATURAL | YSTRIPES)
//                | METRIC "=" (GEODESIC | PLANAR)
//
//   geom-type := GEOMETRY | POINT | LINESTRING | POLYGON | MULTIPOINT
//              | MULTILINESTRING | MULTIPOLYGON | GEOMETRYCOLLECTION
//
// Identifiers are [A-Za-z][A-Za-z0-9_]*, keywords are case-insensitive, and
// each argv[i] is tried against the parsers most-specific-first: primary key,
// then geometry column, then data column.
// ---------------------------------------------------------------------------

#define TG0_CONSTRUCTOR_ERROR "tg0 constructor error: "

// Migration hint for the pre-typed-column tg0 syntax, where every argument
// became an auxiliary column next to an implicit `_shape` column. Bare
// identifiers with no type are how that old syntax shows up to the new parser.
#define TG0_OLD_SYNTAX_HINT                                                    \
  "tg0's syntax changed in v0.0.1-alpha.22: columns now require types, and "   \
  "the _shape column was removed. Drop and recreate this table. See "          \
  "https://github.com/asg017/sqlite-tg/blob/main/docs.md#tg0 for details."

enum Tg0TokenType {
  TG0_TOKEN_TYPE_IDENTIFIER,
  TG0_TOKEN_TYPE_DIGIT,
  TG0_TOKEN_TYPE_EQ,
  TG0_TOKEN_TYPE_COMMA,
};

struct Tg0Token {
  enum Tg0TokenType token_type;
  char *start;
  char *end;
};

static int tg0_is_alpha(char x) {
  return (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z');
}
static int tg0_is_digit(char x) { return (x >= '0' && x <= '9'); }
static int tg0_is_whitespace(char x) {
  return x == ' ' || x == '\t' || x == '\n' || x == '\r';
}

#define TG0_TOKEN_RESULT_EOF 1
#define TG0_TOKEN_RESULT_SOME 2
#define TG0_TOKEN_RESULT_ERROR 3

static int tg0_token_next(char *start, char *end, struct Tg0Token *out) {
  char *ptr = start;
  while (ptr < end) {
    char curr = *ptr;
    if (tg0_is_whitespace(curr)) {
      ptr++;
      continue;
    } else if (curr == '=') {
      ptr++;
      out->start = ptr;
      out->end = ptr;
      out->token_type = TG0_TOKEN_TYPE_EQ;
      return TG0_TOKEN_RESULT_SOME;
    } else if (curr == ',') {
      ptr++;
      out->start = ptr;
      out->end = ptr;
      out->token_type = TG0_TOKEN_TYPE_COMMA;
      return TG0_TOKEN_RESULT_SOME;
    } else if (tg0_is_alpha(curr)) {
      char *startIdentifier = ptr;
      while (ptr < end &&
             (tg0_is_alpha(*ptr) || tg0_is_digit(*ptr) || *ptr == '_')) {
        ptr++;
      }
      out->start = startIdentifier;
      out->end = ptr;
      out->token_type = TG0_TOKEN_TYPE_IDENTIFIER;
      return TG0_TOKEN_RESULT_SOME;
    } else if (tg0_is_digit(curr)) {
      char *startDigit = ptr;
      while (ptr < end && tg0_is_digit(*ptr)) {
        ptr++;
      }
      out->start = startDigit;
      out->end = ptr;
      out->token_type = TG0_TOKEN_TYPE_DIGIT;
      return TG0_TOKEN_RESULT_SOME;
    } else {
      return TG0_TOKEN_RESULT_ERROR;
    }
  }
  return TG0_TOKEN_RESULT_EOF;
}

struct Tg0Scanner {
  char *start;
  char *end;
};

static void tg0_scanner_init(struct Tg0Scanner *scanner, const char *source,
                             int source_length) {
  scanner->start = (char *)source;
  scanner->end = (char *)source + source_length;
}
static int tg0_scanner_next(struct Tg0Scanner *scanner, struct Tg0Token *out) {
  int rc = tg0_token_next(scanner->start, scanner->end, out);
  if (rc == TG0_TOKEN_RESULT_SOME) {
    scanner->start = out->end;
  }
  return rc;
}

// Case-insensitive whole-token keyword match. Compares lengths first, so
// unlike a bare sqlite3_strnicmp(token, keyword, token_length), a token
// that is a strict prefix of keyword (like 'tex' vs 'text') does not match.
static int tg0_token_is(struct Tg0Token *token, const char *keyword) {
  int n = (int)(token->end - token->start);
  return (int)strlen(keyword) == n &&
         sqlite3_strnicmp(token->start, keyword, n) == 0;
}

// Matches token against the geometry type keywords. Returns 0 for the generic
// "geometry" keyword, the corresponding enum tg_geom_type for a narrowing
// type, or -1 when the token is not a geometry type keyword.
static int tg0_token_geom_type(struct Tg0Token *token) {
  if (tg0_token_is(token, "geometry"))
    return 0;
  if (tg0_token_is(token, "point"))
    return TG_POINT;
  if (tg0_token_is(token, "linestring"))
    return TG_LINESTRING;
  if (tg0_token_is(token, "polygon"))
    return TG_POLYGON;
  if (tg0_token_is(token, "multipoint"))
    return TG_MULTIPOINT;
  if (tg0_token_is(token, "multilinestring"))
    return TG_MULTILINESTRING;
  if (tg0_token_is(token, "multipolygon"))
    return TG_MULTIPOLYGON;
  if (tg0_token_is(token, "geometrycollection"))
    return TG_GEOMETRYCOLLECTION;
  return -1;
}

// The canonical keyword for a geometry column's declared type, for
// declare_vtab. geomType is 0 (generic) or an enum tg_geom_type value.
static const char *tg0_geom_type_keyword(int geomType) {
  switch (geomType) {
  case TG_POINT:
    return "point";
  case TG_LINESTRING:
    return "linestring";
  case TG_POLYGON:
    return "polygon";
  case TG_MULTIPOINT:
    return "multipoint";
  case TG_MULTILINESTRING:
    return "multilinestring";
  case TG_MULTIPOLYGON:
    return "multipolygon";
  case TG_GEOMETRYCOLLECTION:
    return "geometrycollection";
  default:
    return "geometry";
  }
}

/**
 * Parse an argv[i] entry of a tg0 virtual table definition, and see if it's a
 * primary key definition: `name integer primary key` | `name text primary
 * key`.
 *
 * out_column_name points into source (same lifetime), out_column_type is
 * SQLITE_INTEGER or SQLITE_TEXT.
 * Returns SQLITE_EMPTY if not a primary key definition, SQLITE_OK if it is.
 */
static int tg0_parse_primary_key_definition(const char *source,
                                            int source_length,
                                            char **out_column_name,
                                            int *out_column_name_length,
                                            int *out_column_type) {
  struct Tg0Scanner scanner;
  struct Tg0Token token;
  char *column_name;
  int column_name_length;
  int column_type;
  int rc;
  tg0_scanner_init(&scanner, source, source_length);

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  column_name = token.start;
  column_name_length = token.end - token.start;

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  if (tg0_token_is(&token, "integer")) {
    column_type = SQLITE_INTEGER;
  } else if (tg0_token_is(&token, "text")) {
    column_type = SQLITE_TEXT;
  } else {
    return SQLITE_EMPTY;
  }

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER ||
      !tg0_token_is(&token, "primary")) {
    return SQLITE_EMPTY;
  }

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER ||
      !tg0_token_is(&token, "key")) {
    return SQLITE_EMPTY;
  }

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_EOF) {
    return SQLITE_EMPTY;
  }

  *out_column_name = column_name;
  *out_column_name_length = column_name_length;
  *out_column_type = column_type;
  return SQLITE_OK;
}

/**
 * Parse an argv[i] entry of a tg0 virtual table definition, and see if it's a
 * geometry column definition: `name <geom-type> [indexing=...] [metric=...]`.
 *
 * out_column_name points into source (same lifetime). out_geom_type is 0 for
 * generic `geometry` columns or an enum tg_geom_type value for narrowed ones.
 * out_indexing is the tg_index to use when parsing stored geometries at query
 * time, TG_DEFAULT unless an `indexing=` option overrides it. out_metric is
 * the distance metric for KNN queries and coordinate validation,
 * TG_METRIC_GEODESIC (lon/lat, meters) unless `metric=planar`.
 * Returns SQLITE_EMPTY if not a geometry column definition, SQLITE_OK if it
 * is, or SQLITE_ERROR when it is one but has a bad option — in which case
 * *out_errmsg is set to an sqlite3_mprintf'd error message.
 */
static int tg0_parse_geometry_column_definition(
    const char *source, int source_length, char **out_column_name,
    int *out_column_name_length, int *out_geom_type,
    enum tg_index *out_indexing, tg_metric *out_metric, char **out_errmsg) {
  struct Tg0Scanner scanner;
  struct Tg0Token token;
  struct Tg0Token key;
  char *column_name;
  int column_name_length;
  int geom_type;
  enum tg_index indexing = TG_DEFAULT;
  tg_metric metric = TG_METRIC_GEODESIC;
  int rc;
  tg0_scanner_init(&scanner, source, source_length);

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  column_name = token.start;
  column_name_length = token.end - token.start;

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  geom_type = tg0_token_geom_type(&token);
  if (geom_type < 0) {
    return SQLITE_EMPTY;
  }

  // optional `key=value` options: `indexing=none|natural|ystripes` and
  // `metric=geodesic|planar`. A recognizable option shape with an unknown
  // key or value is a hard error, anything structurally off falls back to
  // the caller's generic could-not-parse error.
  while (1) {
    rc = tg0_scanner_next(&scanner, &key);
    if (rc == TG0_TOKEN_RESULT_EOF) {
      break;
    }
    if (rc != TG0_TOKEN_RESULT_SOME ||
        key.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
      return SQLITE_EMPTY;
    }
    rc = tg0_scanner_next(&scanner, &token);
    if (rc != TG0_TOKEN_RESULT_SOME || token.token_type != TG0_TOKEN_TYPE_EQ) {
      return SQLITE_EMPTY;
    }
    rc = tg0_scanner_next(&scanner, &token);
    if (rc != TG0_TOKEN_RESULT_SOME ||
        (token.token_type != TG0_TOKEN_TYPE_IDENTIFIER &&
         token.token_type != TG0_TOKEN_TYPE_DIGIT)) {
      return SQLITE_EMPTY;
    }
    if (tg0_token_is(&key, "indexing")) {
      if (tg0_token_is(&token, "none")) {
        indexing = TG_NONE;
      } else if (tg0_token_is(&token, "natural")) {
        indexing = TG_NATURAL;
      } else if (tg0_token_is(&token, "ystripes")) {
        indexing = TG_YSTRIPES;
      } else {
        *out_errmsg = sqlite3_mprintf(
            TG0_CONSTRUCTOR_ERROR
            "unknown indexing option '%.*s' in '%.*s', expected one of none, "
            "natural, or ystripes",
            (int)(token.end - token.start), token.start, source_length, source);
        return SQLITE_ERROR;
      }
    } else if (tg0_token_is(&key, "metric")) {
      if (tg0_token_is(&token, "geodesic")) {
        metric = TG_METRIC_GEODESIC;
      } else if (tg0_token_is(&token, "planar")) {
        metric = TG_METRIC_PLANAR;
      } else {
        *out_errmsg = sqlite3_mprintf(
            TG0_CONSTRUCTOR_ERROR
            "unknown metric option '%.*s' in '%.*s', expected geodesic or "
            "planar",
            (int)(token.end - token.start), token.start, source_length, source);
        return SQLITE_ERROR;
      }
    } else {
      *out_errmsg = sqlite3_mprintf(
          TG0_CONSTRUCTOR_ERROR
          "unknown option '%.*s' in '%.*s', only 'indexing=' and 'metric=' "
          "are supported on geometry columns",
          (int)(key.end - key.start), key.start, source_length, source);
      return SQLITE_ERROR;
    }
  }

  *out_column_name = column_name;
  *out_column_name_length = column_name_length;
  *out_geom_type = geom_type;
  *out_indexing = indexing;
  *out_metric = metric;
  return SQLITE_OK;
}

/**
 * Parse an argv[i] entry of a tg0 virtual table definition, and see if it's a
 * plain data column definition: `name text|int|integer|float|double|blob`.
 *
 * out_column_name points into source (same lifetime), out_column_type is one
 * of SQLITE_TEXT, SQLITE_INTEGER, SQLITE_FLOAT, or SQLITE_BLOB.
 * Returns SQLITE_EMPTY if not a data column definition, SQLITE_OK if it is,
 * or SQLITE_ERROR (with *out_errmsg set) for an `indexing=` option on a data
 * column.
 */
static int tg0_parse_data_column_definition(const char *source,
                                            int source_length,
                                            char **out_column_name,
                                            int *out_column_name_length,
                                            int *out_column_type,
                                            char **out_errmsg) {
  struct Tg0Scanner scanner;
  struct Tg0Token token;
  char *column_name;
  int column_name_length;
  int column_type;
  int rc;
  tg0_scanner_init(&scanner, source, source_length);

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  column_name = token.start;
  column_name_length = token.end - token.start;

  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  if (tg0_token_is(&token, "text")) {
    column_type = SQLITE_TEXT;
  } else if (tg0_token_is(&token, "int") || tg0_token_is(&token, "integer")) {
    column_type = SQLITE_INTEGER;
  } else if (tg0_token_is(&token, "float") || tg0_token_is(&token, "double")) {
    column_type = SQLITE_FLOAT;
  } else if (tg0_token_is(&token, "blob")) {
    column_type = SQLITE_BLOB;
  } else {
    return SQLITE_EMPTY;
  }

  rc = tg0_scanner_next(&scanner, &token);
  if (rc == TG0_TOKEN_RESULT_SOME &&
      token.token_type == TG0_TOKEN_TYPE_IDENTIFIER &&
      (tg0_token_is(&token, "indexing") || tg0_token_is(&token, "metric"))) {
    *out_errmsg = sqlite3_mprintf(
        TG0_CONSTRUCTOR_ERROR
        "'%.*s=' is only supported on geometry columns, in '%.*s'",
        (int)(token.end - token.start), token.start, source_length, source);
    return SQLITE_ERROR;
  }
  if (rc != TG0_TOKEN_RESULT_EOF) {
    return SQLITE_EMPTY;
  }

  *out_column_name = column_name;
  *out_column_name_length = column_name_length;
  *out_column_type = column_type;
  return SQLITE_OK;
}

// Returns 1 when the whole source is a single bare identifier — how a column
// of the removed pre-typed tg0 syntax (`tg0(aux1, aux2)`) looks to the new
// parser. Used to attach the migration hint to could-not-parse errors.
static int tg0_is_bare_identifier(const char *source, int source_length) {
  struct Tg0Scanner scanner;
  struct Tg0Token token;
  int rc;
  tg0_scanner_init(&scanner, source, source_length);
  rc = tg0_scanner_next(&scanner, &token);
  if (rc != TG0_TOKEN_RESULT_SOME ||
      token.token_type != TG0_TOKEN_TYPE_IDENTIFIER) {
    return 0;
  }
  return tg0_scanner_next(&scanner, &token) == TG0_TOKEN_RESULT_EOF;
}

// ---------------------------------------------------------------------------
// tg0 column model
// ---------------------------------------------------------------------------

typedef enum {
  // geometry column, ie `boundary polygon indexing=natural`
  TG0_USER_COLUMN_KIND_GEOMETRY = 1,
  // plain data column, ie `label text`
  TG0_USER_COLUMN_KIND_DATA = 2,
} tg0_user_column_kind;

struct tg0_geometry_column {
  // column name. Must be freed with sqlite3_free() on destruction
  char *name;
  // 0 for generic `geometry` columns (accepts all 7 tg types), otherwise the
  // enum tg_geom_type this column is narrowed to, enforced on write
  int geomType;
  // the tg_index used when parsing stored geometries at query time
  // (`indexing=` option, defaults to TG_DEFAULT)
  enum tg_index indexing;
  // distance metric for KNN queries (`metric=` option, defaults to
  // TG_METRIC_GEODESIC). Geodesic columns also validate that every written
  // coordinate is a finite lon/lat pair, planar columns that it is finite
  tg_metric metric;
};

struct tg0_data_column {
  // column name. Must be freed with sqlite3_free() on destruction
  char *name;
  // declared type, enforced on write: SQLITE_TEXT, SQLITE_INTEGER,
  // SQLITE_FLOAT, or SQLITE_BLOB
  int type;
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

  // 0 when no explicit primary key column was declared, otherwise
  // SQLITE_INTEGER (rowid alias) or SQLITE_TEXT (WITHOUT ROWID + rowids
  // mapping shadow table)
  int pkColumnType;
  // name of the declared primary key column, or NULL when none was declared.
  // Must be freed with sqlite3_free() on destruction
  char *pkColumnName;

  // number of declared geometry columns, 1 <= n <= TG0_MAX_GEOMETRY_COLUMNS
  int numGeometryColumns;
  // number of declared data columns, 0 <= n <= TG0_MAX_DATA_COLUMNS
  int numDataColumns;

  // kind of the i'th user column, in declaration order. User columns exclude
  // the primary key (always declared column 0 when present) and the hidden
  // distance column (always declared last). Only the first
  // (numGeometryColumns + numDataColumns) entries are meaningful.
  tg0_user_column_kind user_column_kinds[TG0_MAX_USER_COLUMNS];
  // index of the i'th user column into geometry_columns[] or data_columns[],
  // depending on user_column_kinds[i]
  int user_column_idxs[TG0_MAX_USER_COLUMNS];

  // Only the first numGeometryColumns entries are meaningful.
  struct tg0_geometry_column geometry_columns[TG0_MAX_GEOMETRY_COLUMNS];
  // Only the first numDataColumns entries are meaningful.
  struct tg0_data_column data_columns[TG0_MAX_DATA_COLUMNS];

  // Cached shadow-table write statements (vec0-style): prepared lazily on
  // first use, reset + cleared after every use, finalized by
  // tg0_free_statements() on destruction and before a rename (their SQL
  // embeds the table name). Preparing per row made bulk loads ~3x slower
  // than the rtree inserts themselves.
  sqlite3_stmt *stmtRowidsInsert;
  sqlite3_stmt *stmtRowidsLookup;
  sqlite3_stmt *stmtRowidsDelete;
  sqlite3_stmt *stmtContentInsert;
  sqlite3_stmt *stmtContentDelete;
  sqlite3_stmt *stmtRtreeInsert[TG0_MAX_GEOMETRY_COLUMNS];
  sqlite3_stmt *stmtRtreeDelete[TG0_MAX_GEOMETRY_COLUMNS];
  sqlite3_stmt *stmtRtreeUpdate[TG0_MAX_GEOMETRY_COLUMNS];
};

// Finalizes every cached statement; safe to call repeatedly.
static void tg0_free_statements(tg0_vtab *p) {
  sqlite3_finalize(p->stmtRowidsInsert);
  sqlite3_finalize(p->stmtRowidsLookup);
  sqlite3_finalize(p->stmtRowidsDelete);
  sqlite3_finalize(p->stmtContentInsert);
  sqlite3_finalize(p->stmtContentDelete);
  p->stmtRowidsInsert = p->stmtRowidsLookup = p->stmtRowidsDelete = NULL;
  p->stmtContentInsert = p->stmtContentDelete = NULL;
  for (int i = 0; i < TG0_MAX_GEOMETRY_COLUMNS; i++) {
    sqlite3_finalize(p->stmtRtreeInsert[i]);
    sqlite3_finalize(p->stmtRtreeDelete[i]);
    sqlite3_finalize(p->stmtRtreeUpdate[i]);
    p->stmtRtreeInsert[i] = p->stmtRtreeDelete[i] = p->stmtRtreeUpdate[i] =
        NULL;
  }
}

// Number of non-pk, non-hidden user columns.
static int tg0_num_user_columns(tg0_vtab *p) {
  return p->numGeometryColumns + p->numDataColumns;
}

// 1 when a primary key column was declared (it occupies declared column 0),
// 0 otherwise. Add this to a user column's position to get its declared
// column index, ie the `i` that xColumn receives.
static int tg0_pk_column_offset(tg0_vtab *p) {
  return p->pkColumnName ? 1 : 0;
}

// Declared column index of the hidden `distance` column (reserved for KNN).
static int tg0_column_distance_idx(tg0_vtab *p) {
  return tg0_pk_column_offset(p) + tg0_num_user_columns(p);
}

// Declared column index of the i'th user column.
static int tg0_user_column_idx(tg0_vtab *p, int iUser) {
  return tg0_pk_column_offset(p) + iUser;
}

// Map a declared column index (as passed to xColumn/xBestIndex) to its user
// column kind and its index into geometry_columns[]/data_columns[]. Returns
// SQLITE_OK on success, or SQLITE_ERROR when iColumn is the primary key
// column, the hidden distance column, or out of range.
static int tg0_column_lookup(tg0_vtab *p, int iColumn,
                             tg0_user_column_kind *outKind, int *outIdx) {
  int iUser = iColumn - tg0_pk_column_offset(p);
  if (iColumn < tg0_pk_column_offset(p) || iUser >= tg0_num_user_columns(p)) {
    return SQLITE_ERROR;
  }
  if (outKind) {
    *outKind = p->user_column_kinds[iUser];
  }
  if (outIdx) {
    *outIdx = p->user_column_idxs[iUser];
  }
  return SQLITE_OK;
}

// Frees a tg0_vtab and everything it owns. Safe on partially-initialized
// vtabs (all fields start zeroed).
static void tg0_free(tg0_vtab *p) {
  if (!p) {
    return;
  }
  tg0_free_statements(p);
  sqlite3_free(p->schemaName);
  sqlite3_free(p->tableName);
  sqlite3_free(p->pkColumnName);
  for (int i = 0; i < p->numGeometryColumns; i++) {
    sqlite3_free(p->geometry_columns[i].name);
  }
  for (int i = 0; i < p->numDataColumns; i++) {
    sqlite3_free(p->data_columns[i].name);
  }
  sqlite3_free(p);
}

// `distance` is the hidden KNN column, `rowid` conflicts with SQLite's own
// rowid naming on rowid tables.
static int tg0_column_name_reserved(const char *name, int nName) {
  return (nName == 8 && sqlite3_strnicmp(name, "distance", 8) == 0) ||
         (nName == 5 && sqlite3_strnicmp(name, "rowid", 5) == 0);
}

static int tg0_column_name_taken(tg0_vtab *p, const char *name, int nName) {
  if (p->pkColumnName && (int)strlen(p->pkColumnName) == nName &&
      sqlite3_strnicmp(p->pkColumnName, name, nName) == 0) {
    return 1;
  }
  for (int i = 0; i < p->numGeometryColumns; i++) {
    if ((int)strlen(p->geometry_columns[i].name) == nName &&
        sqlite3_strnicmp(p->geometry_columns[i].name, name, nName) == 0) {
      return 1;
    }
  }
  for (int i = 0; i < p->numDataColumns; i++) {
    if ((int)strlen(p->data_columns[i].name) == nName &&
        sqlite3_strnicmp(p->data_columns[i].name, name, nName) == 0) {
      return 1;
    }
  }
  return 0;
}

// Validates a newly parsed user column name against the reserved names and
// the columns declared so far. Returns SQLITE_OK when the name is fine,
// SQLITE_ERROR with *pzErr set to an sqlite3_mprintf'd message when it is
// not, or SQLITE_NOMEM when building that message fails — so an OOM can
// never read as the name having passed the check.
static int tg0_check_column_name(tg0_vtab *p, const char *arg,
                                 const char *name, int nName, char **pzErr) {
  const char *zProblem;
  if (tg0_column_name_reserved(name, nName)) {
    zProblem = "reserved";
  } else if (tg0_column_name_taken(p, name, nName)) {
    zProblem = "duplicate";
  } else {
    return SQLITE_OK;
  }
  *pzErr = sqlite3_mprintf(TG0_CONSTRUCTOR_ERROR "%s column name '%.*s' in '%s'",
                           zProblem, nName, name, arg);
  return *pzErr ? SQLITE_ERROR : SQLITE_NOMEM;
}

// ---------------------------------------------------------------------------
// tg0 shadow tables
//
// For a tg0 table `t` with geometry columns g0..gN and data columns d0..dM
// (plan.md "Shadow tables"):
//
//   t_info(key TEXT PRIMARY KEY, value ANY)   -- 'CREATE_VERSION' row; future
//                                                migration hook
//   t_content(rowid INTEGER PRIMARY KEY,
//             geom00 BLOB NOT NULL, ..., data00, ...)
//   t_rtree00 .. t_rtreeNN                    -- rtree(id, minX, maxX, minY,
//                                                maxY), one per geometry column
//   t_rowids(rowid INTEGER PRIMARY KEY AUTOINCREMENT,
//            id TEXT UNIQUE NOT NULL)         -- text primary keys only
//
// Row data lives in t_content, so rowid/primary key lookups are B-tree point
// queries, and the rtrees hold nothing but float32 bboxes. Numbered internal
// names (geom00, data00, rtree00 — NN is the index into geometry_columns[]
// or data_columns[]) avoid identifier-quoting issues: user column names
// exist only in declare_vtab. Data columns are declared with no affinity;
// their declared types are enforced in C on write.
// ---------------------------------------------------------------------------

// Schema-qualified, %w-quoted shadow table names, filled in with
// sqlite3_mprintf(..., schemaName, tableName). TG0_SHADOW_RTREE_NAME
// additionally takes the geometry column's index into geometry_columns[].
#define TG0_SHADOW_INFO_NAME "\"%w\".\"%w_info\""
#define TG0_SHADOW_CONTENT_NAME "\"%w\".\"%w_content\""
#define TG0_SHADOW_ROWIDS_NAME "\"%w\".\"%w_rowids\""
#define TG0_SHADOW_RTREE_NAME "\"%w\".\"%w_rtree%02d\""
#define TG0_SHADOW_RTREE_NODE_NAME "\"%w\".\"%w_rtree%02d_node\""

// Prepares and steps a single shadow-table DDL/DML statement. zSql is an
// sqlite3_mprintf'd string, freed here; a NULL zSql (OOM in the caller's
// sqlite3_mprintf) returns SQLITE_NOMEM. On a prepare/step failure, returns
// SQLITE_ERROR with *pzErrMsg (when pzErrMsg is non-NULL) set to an
// sqlite3_mprintf'd "<zWhat>: <sqlite3_errmsg>" message.
static int tg0_shadow_sql(sqlite3 *db, char *zSql, const char *zWhat,
                          char **pzErrMsg) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  if (!zSql) {
    return SQLITE_NOMEM;
  }
  rc = sqlite3_prepare_v2(db, zSql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) {
    if (pzErrMsg) {
      *pzErrMsg = sqlite3_mprintf("%s: %s", zWhat, sqlite3_errmsg(db));
    }
    rc = SQLITE_ERROR;
  } else {
    rc = SQLITE_OK;
  }
  sqlite3_finalize(stmt);
  sqlite3_free(zSql);
  return rc;
}

// Creates the shadow tables for a freshly xCreate'd tg0 table and seeds
// t_info with its CREATE_VERSION row. Stops at the first failure: returns
// SQLITE_NOMEM, or SQLITE_ERROR with *pzErrMsg set — the caller cleans up
// whatever was created before the failure with tg0_drop_shadow_tables().
static int tg0_create_shadow_tables(tg0_vtab *p, char **pzErrMsg) {
  int rc;

  rc = tg0_shadow_sql(p->db,
                      sqlite3_mprintf("CREATE TABLE " TG0_SHADOW_INFO_NAME
                                      "(key TEXT PRIMARY KEY, value ANY)",
                                      p->schemaName, p->tableName),
                      "Could not create the tg0 info shadow table", pzErrMsg);
  if (rc != SQLITE_OK) {
    return rc;
  }

  rc = tg0_shadow_sql(
      p->db,
      sqlite3_mprintf("INSERT INTO " TG0_SHADOW_INFO_NAME
                      "(key, value) VALUES ('CREATE_VERSION', %Q)",
                      p->schemaName, p->tableName, SQLITE_TG_VERSION),
      "Could not seed the tg0 info shadow table", pzErrMsg);
  if (rc != SQLITE_OK) {
    return rc;
  }

  sqlite3_str *strContent = sqlite3_str_new(NULL);
  sqlite3_str_appendf(strContent,
                      "CREATE TABLE " TG0_SHADOW_CONTENT_NAME
                      "(rowid INTEGER PRIMARY KEY",
                      p->schemaName, p->tableName);
  for (int i = 0; i < p->numGeometryColumns; i++) {
    sqlite3_str_appendf(strContent, ", geom%02d BLOB NOT NULL", i);
  }
  for (int i = 0; i < p->numDataColumns; i++) {
    sqlite3_str_appendf(strContent, ", data%02d", i);
  }
  sqlite3_str_appendall(strContent, ")");
  rc = tg0_shadow_sql(p->db, sqlite3_str_finish(strContent),
                      "Could not create the tg0 content shadow table",
                      pzErrMsg);
  if (rc != SQLITE_OK) {
    return rc;
  }

  for (int i = 0; i < p->numGeometryColumns; i++) {
    rc = tg0_shadow_sql(
        p->db,
        sqlite3_mprintf("CREATE VIRTUAL TABLE " TG0_SHADOW_RTREE_NAME
                        " USING rtree(id, minX, maxX, minY, maxY)",
                        p->schemaName, p->tableName, i),
        "Could not create a tg0 rtree shadow table", pzErrMsg);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

  if (p->pkColumnType == SQLITE_TEXT) {
    rc = tg0_shadow_sql(
        p->db,
        sqlite3_mprintf("CREATE TABLE " TG0_SHADOW_ROWIDS_NAME
                        "(rowid INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "id TEXT UNIQUE NOT NULL)",
                        p->schemaName, p->tableName),
        "Could not create the tg0 rowids shadow table", pzErrMsg);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

  return SQLITE_OK;
}

// Drops every shadow table belonging to p: info, content, one rtree per
// geometry column, and rowids for text-primary-key tables. Every drop is
// attempted even after a failure (the first failure is the one reported),
// and IF EXISTS keeps a missing table — a partially-created table being
// cleaned up after an xCreate failure — from counting as one. Returns
// SQLITE_OK, or the first failure's SQLITE_NOMEM/SQLITE_ERROR with *pzErrMsg
// (when pzErrMsg is non-NULL) set to an sqlite3_mprintf'd message.
static int tg0_drop_shadow_tables(tg0_vtab *p, char **pzErrMsg) {
  int rc = SQLITE_OK;
  int n = 0;
  char *azSql[3 + TG0_MAX_GEOMETRY_COLUMNS];

  azSql[n++] = sqlite3_mprintf("DROP TABLE IF EXISTS " TG0_SHADOW_INFO_NAME,
                               p->schemaName, p->tableName);
  azSql[n++] = sqlite3_mprintf("DROP TABLE IF EXISTS " TG0_SHADOW_CONTENT_NAME,
                               p->schemaName, p->tableName);
  for (int i = 0; i < p->numGeometryColumns; i++) {
    azSql[n++] = sqlite3_mprintf("DROP TABLE IF EXISTS " TG0_SHADOW_RTREE_NAME,
                                 p->schemaName, p->tableName, i);
  }
  if (p->pkColumnType == SQLITE_TEXT) {
    azSql[n++] = sqlite3_mprintf("DROP TABLE IF EXISTS " TG0_SHADOW_ROWIDS_NAME,
                                 p->schemaName, p->tableName);
  }
  for (int i = 0; i < n; i++) {
    int rc2 = tg0_shadow_sql(p->db, azSql[i],
                             "Could not drop a tg0 shadow table",
                             rc == SQLITE_OK ? pzErrMsg : NULL);
    if (rc == SQLITE_OK) {
      rc = rc2;
    }
  }
  return rc;
}

typedef struct tg0_cursor tg0_cursor;
struct tg0_cursor {
  sqlite3_vtab_cursor base;
  // current scan over t_content (joined with t_rowids on text-primary-key
  // tables and, on predicate plans, the geometry column's rtree), prepared
  // by tg0Filter. NULL before the first xFilter call
  sqlite3_stmt *stmt;
  // 1 only on real scan completion (SQLITE_DONE). A mid-scan step failure
  // returns its error from xFilter/xNext and leaves eof unset, so the
  // failure can never read as a clean end-of-results
  int eof;
  // the current plan's idxStr header character (TG0_PLAN_*)
  char plan;
  // predicate plans only: index into p->geometry_columns[] of the scanned
  // geometry column, the exact tg predicate applied per candidate row, and
  // the parsed query geometry (owned; freed on re-Filter and xClose)
  int predGeomIdx;
  GeomPredicateFunc predFunc;
  struct tg_geom *queryGeom;
  // KNN plans only: the best-first traversal state (owned; freed on
  // re-Filter and xClose). On KNN plans stmt is the by-rowid content fetch,
  // positioned on the current row
  struct tg0_knn *knn;
};

// One row per intercepted predicate: the xFindFunction constraint op, its
// idxStr [op] letter, its rtree prefilter [strategy] letter (plan.md decision
// 8), and the exact tg predicate applied to each candidate row. The exact
// predicate is always called as func(candidate, query), matching
// tg_predicate_impl's tg_pred(column-value, argument) argument order.
static const struct {
  int constraintOp; // TG0_FUNC_*
  char opChar;
  char strategy;
  GeomPredicateFunc func;
} tg0_predicate_plans[] = {
    {TG0_FUNC_INTERSECTS, 'I', TG0_STRATEGY_OVERLAP, tg_geom_intersects},
    {TG0_FUNC_TOUCHES, 'T', TG0_STRATEGY_OVERLAP, tg_geom_touches},
    {TG0_FUNC_EQUALS, 'E', TG0_STRATEGY_OVERLAP, tg_geom_equals},
    {TG0_FUNC_CONTAINS, 'C', TG0_STRATEGY_CONTAINS, tg_geom_contains},
    {TG0_FUNC_COVERS, 'V', TG0_STRATEGY_CONTAINS, tg_geom_covers},
    {TG0_FUNC_WITHIN, 'W', TG0_STRATEGY_WITHIN, tg_geom_within},
    {TG0_FUNC_COVEREDBY, 'B', TG0_STRATEGY_WITHIN, tg_geom_coveredby},
    {TG0_FUNC_DISJOINT, 'D', TG0_STRATEGY_FULLSCAN, tg_geom_disjoint},
};
#define TG0_NUM_PREDICATE_PLANS                                                \
  ((int)(sizeof(tg0_predicate_plans) / sizeof(tg0_predicate_plans[0])))

// The tg0_predicate_plans[] row for an xBestIndex constraint op, or -1.
static int tg0_predicate_plan_by_op(int constraintOp) {
  for (int i = 0; i < TG0_NUM_PREDICATE_PLANS; i++) {
    if (tg0_predicate_plans[i].constraintOp == constraintOp) {
      return i;
    }
  }
  return -1;
}

// The tg0_predicate_plans[] row for an idxStr [op] letter, or -1.
static int tg0_predicate_plan_by_char(char opChar) {
  for (int i = 0; i < TG0_NUM_PREDICATE_PLANS; i++) {
    if (tg0_predicate_plans[i].opChar == opChar) {
      return i;
    }
  }
  return -1;
}

// The idxStr pushdown [cmp-op] letter for an xBestIndex constraint op, or 0
// when the op is not pushdown-eligible.
static char tg0_pushdown_op_char(int constraintOp) {
  switch (constraintOp) {
  case SQLITE_INDEX_CONSTRAINT_EQ:
    return TG0_PUSHDOWN_OP_EQ;
  case SQLITE_INDEX_CONSTRAINT_GT:
    return TG0_PUSHDOWN_OP_GT;
  case SQLITE_INDEX_CONSTRAINT_LE:
    return TG0_PUSHDOWN_OP_LE;
  case SQLITE_INDEX_CONSTRAINT_LT:
    return TG0_PUSHDOWN_OP_LT;
  case SQLITE_INDEX_CONSTRAINT_GE:
    return TG0_PUSHDOWN_OP_GE;
  case SQLITE_INDEX_CONSTRAINT_NE:
    return TG0_PUSHDOWN_OP_NE;
  default:
    return 0;
  }
}

// The SQL comparison operator for an idxStr pushdown [cmp-op] letter, or
// NULL when the letter is not part of the alphabet.
static const char *tg0_pushdown_op_sql(char opChar) {
  switch (opChar) {
  case TG0_PUSHDOWN_OP_EQ:
    return "=";
  case TG0_PUSHDOWN_OP_GT:
    return ">";
  case TG0_PUSHDOWN_OP_LE:
    return "<=";
  case TG0_PUSHDOWN_OP_LT:
    return "<";
  case TG0_PUSHDOWN_OP_GE:
    return ">=";
  case TG0_PUSHDOWN_OP_NE:
    return "<>";
  default:
    return NULL;
  }
}

void tg_vtab_set_error(sqlite3_vtab *pVTab, const char *zFormat, ...) {
  va_list args;
  sqlite3_free(pVTab->zErrMsg);
  va_start(args, zFormat);
  pVTab->zErrMsg = sqlite3_vmprintf(zFormat, args);
  va_end(args);
}

int db_supports_rtree(sqlite3*db) {
  sqlite3_stmt * stmt;
  int rc = sqlite3_prepare_v2(db, "select name from pragma_module_list where name = 'rtree';", -1, &stmt, NULL);
  // TODO could be that PRAGMA module_list is not allowed
  if(rc != SQLITE_OK) {
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_ROW;
}

static int tg0_init(sqlite3 *db, void *pAux, int argc, const char *const *argv,
                    sqlite3_vtab **ppVtab, char **pzErr, bool isCreate) {
  tg0_vtab *pNew;
  int rc;
  if(!db_supports_rtree(db)) {
    *pzErr = sqlite3_mprintf("The current SQLite connection does not include the R-Tree extension, which is required by tg0.");
    return SQLITE_ERROR;
  }

  pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew == 0) {
    return SQLITE_NOMEM;
  }
  memset(pNew, 0, sizeof(*pNew));

  for (int i = 3; i < argc; i++) {
    const char *arg = argv[i];
    int nArg = (int)strlen(arg);
    char *cName = NULL;
    int nName = 0;
    int columnType = 0;
    int geomType = 0;
    enum tg_index indexing = TG_DEFAULT;
    tg_metric metric = TG_METRIC_GEODESIC;
    char *errmsg = NULL;

    // 1) primary key column, ie `id integer primary key`
    rc = tg0_parse_primary_key_definition(arg, nArg, &cName, &nName,
                                          &columnType);
    if (rc == SQLITE_OK) {
      if (pNew->pkColumnName) {
        *pzErr = sqlite3_mprintf(
            TG0_CONSTRUCTOR_ERROR
            "more than one primary key column was declared ('%s'), only one "
            "primary key is allowed",
            arg);
        goto error;
      }
      rc = tg0_check_column_name(pNew, arg, cName, nName, pzErr);
      if (rc == SQLITE_NOMEM) {
        goto nomem;
      }
      if (rc != SQLITE_OK) {
        goto error;
      }
      pNew->pkColumnType = columnType;
      pNew->pkColumnName = sqlite3_mprintf("%.*s", nName, cName);
      if (!pNew->pkColumnName) {
        goto nomem;
      }
      continue;
    }

    // 2) geometry column, ie `boundary polygon indexing=natural`
    rc = tg0_parse_geometry_column_definition(
        arg, nArg, &cName, &nName, &geomType, &indexing, &metric, &errmsg);
    if (rc == SQLITE_ERROR) {
      *pzErr = errmsg;
      goto error;
    }
    if (rc == SQLITE_OK) {
      if (pNew->numGeometryColumns >= TG0_MAX_GEOMETRY_COLUMNS) {
        *pzErr = sqlite3_mprintf(
            TG0_CONSTRUCTOR_ERROR
            "too many geometry columns ('%s'), a maximum of %d geometry "
            "columns are allowed",
            arg, TG0_MAX_GEOMETRY_COLUMNS);
        goto error;
      }
      rc = tg0_check_column_name(pNew, arg, cName, nName, pzErr);
      if (rc == SQLITE_NOMEM) {
        goto nomem;
      }
      if (rc != SQLITE_OK) {
        goto error;
      }
      struct tg0_geometry_column *column =
          &pNew->geometry_columns[pNew->numGeometryColumns];
      column->name = sqlite3_mprintf("%.*s", nName, cName);
      if (!column->name) {
        goto nomem;
      }
      column->geomType = geomType;
      column->indexing = indexing;
      column->metric = metric;
      int iUser = tg0_num_user_columns(pNew);
      pNew->user_column_kinds[iUser] = TG0_USER_COLUMN_KIND_GEOMETRY;
      pNew->user_column_idxs[iUser] = pNew->numGeometryColumns;
      pNew->numGeometryColumns++;
      continue;
    }

    // 3) data column, ie `label text`
    rc = tg0_parse_data_column_definition(arg, nArg, &cName, &nName,
                                          &columnType, &errmsg);
    if (rc == SQLITE_ERROR) {
      *pzErr = errmsg;
      goto error;
    }
    if (rc == SQLITE_OK) {
      if (pNew->numDataColumns >= TG0_MAX_DATA_COLUMNS) {
        *pzErr = sqlite3_mprintf(
            TG0_CONSTRUCTOR_ERROR
            "too many data columns ('%s'), a maximum of %d data columns are "
            "allowed",
            arg, TG0_MAX_DATA_COLUMNS);
        goto error;
      }
      rc = tg0_check_column_name(pNew, arg, cName, nName, pzErr);
      if (rc == SQLITE_NOMEM) {
        goto nomem;
      }
      if (rc != SQLITE_OK) {
        goto error;
      }
      struct tg0_data_column *column =
          &pNew->data_columns[pNew->numDataColumns];
      column->name = sqlite3_mprintf("%.*s", nName, cName);
      if (!column->name) {
        goto nomem;
      }
      column->type = columnType;
      int iUser = tg0_num_user_columns(pNew);
      pNew->user_column_kinds[iUser] = TG0_USER_COLUMN_KIND_DATA;
      pNew->user_column_idxs[iUser] = pNew->numDataColumns;
      pNew->numDataColumns++;
      continue;
    }

    if (tg0_is_bare_identifier(arg, nArg)) {
      *pzErr = sqlite3_mprintf(
          TG0_CONSTRUCTOR_ERROR "could not parse '%s'. " TG0_OLD_SYNTAX_HINT,
          arg);
    } else {
      *pzErr =
          sqlite3_mprintf(TG0_CONSTRUCTOR_ERROR "could not parse '%s'", arg);
    }
    goto error;
  }

  if (pNew->numGeometryColumns < 1) {
    *pzErr = sqlite3_mprintf(TG0_CONSTRUCTOR_ERROR
                             "at least one geometry column is required");
    goto error;
  }

  // declare_vtab: primary key (if any), then user columns in declaration
  // order, then the hidden `distance` column (reserved for KNN). Text
  // primary keys make the table WITHOUT ROWID; a rowids mapping shadow table
  // supplies the int64 rowids that rtrees need.
  sqlite3_str *strSchema = sqlite3_str_new(NULL);
  sqlite3_str_appendall(strSchema, "CREATE TABLE x(");
  if (pNew->pkColumnName) {
    sqlite3_str_appendf(strSchema, "\"%w\" %s primary key, ",
                        pNew->pkColumnName,
                        pNew->pkColumnType == SQLITE_TEXT ? "text" : "integer");
  }
  for (int i = 0; i < tg0_num_user_columns(pNew); i++) {
    int idx = pNew->user_column_idxs[i];
    if (pNew->user_column_kinds[i] == TG0_USER_COLUMN_KIND_GEOMETRY) {
      sqlite3_str_appendf(
          strSchema, "\"%w\" %s, ", pNew->geometry_columns[idx].name,
          tg0_geom_type_keyword(pNew->geometry_columns[idx].geomType));
    } else {
      const char *zType;
      switch (pNew->data_columns[idx].type) {
      case SQLITE_INTEGER:
        zType = "integer";
        break;
      case SQLITE_FLOAT:
        zType = "float";
        break;
      case SQLITE_BLOB:
        zType = "blob";
        break;
      default:
        zType = "text";
        break;
      }
      sqlite3_str_appendf(strSchema, "\"%w\" %s, ",
                          pNew->data_columns[idx].name, zType);
    }
  }
  sqlite3_str_appendall(strSchema, "distance hidden)");
  if (pNew->pkColumnType == SQLITE_TEXT) {
    sqlite3_str_appendall(strSchema, " WITHOUT ROWID");
  }
  const char *zSchema = sqlite3_str_finish(strSchema);
  if (!zSchema) {
    goto nomem;
  }
  rc = sqlite3_declare_vtab(db, zSchema);
  sqlite3_free((void *)zSchema);
  if (rc != SQLITE_OK) {
    *pzErr =
        sqlite3_mprintf("Error declaring vtab schema for tg0 virtual table: %s",
                        sqlite3_errmsg(db));
    goto error;
  }

  pNew->db = db;
  pNew->schemaName = sqlite3_mprintf("%s", argv[1]);
  pNew->tableName = sqlite3_mprintf("%s", argv[2]);
  if (!pNew->schemaName || !pNew->tableName) {
    goto nomem;
  }

  if (isCreate) {
    rc = tg0_create_shadow_tables(pNew, pzErr);
    if (rc != SQLITE_OK) {
      // best-effort removal of the shadow tables created before the failure
      tg0_drop_shadow_tables(pNew, NULL);
      if (rc == SQLITE_NOMEM) {
        goto nomem;
      }
      goto error;
    }
  }

  *ppVtab = (sqlite3_vtab *)pNew;
  return SQLITE_OK;

nomem:
  tg0_free(pNew);
  return SQLITE_NOMEM;

error:
  // xDisconnect is not called when xCreate/xConnect fail, so clean up here
  tg0_free(pNew);
  return SQLITE_ERROR;
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
  tg0_free((tg0_vtab *)pVtab);
  return SQLITE_OK;
}
static int tg0Destroy(sqlite3_vtab *pVtab) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  char *zErrMsg = NULL;
  int rc = tg0_drop_shadow_tables(p, &zErrMsg);
  if (rc != SQLITE_OK) {
    // the DROP TABLE fails and the virtual table stays connected, so report
    // the failure on the vtab and leave it alive
    sqlite3_free(pVtab->zErrMsg);
    pVtab->zErrMsg = zErrMsg;
    return rc;
  }
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

static void tg0_knn_free(struct tg0_knn *knn);

static int tg0Close(sqlite3_vtab_cursor *cur) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  sqlite3_finalize(pCur->stmt);
  tg_geom_free(pCur->queryGeom);
  tg0_knn_free(pCur->knn);
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int tg0BestIndex(sqlite3_vtab *pVtab, sqlite3_index_info *pIdxInfo) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  int iPointTerm = -1;
  int iPredicateTerm = -1;
  int predicatePlan = -1;
  int predicateGeomIdx = -1;
  int iKnnTerm = -1;
  int knnGeomIdx = -1;
  int hasUnusableMatch = 0;
  sqlite3_int64 limit = -1;
  // KNN folds: the first `distance <`/`<=` term, and the first predicate on
  // the MATCH column (preferring one with an rtree prune strategy)
  int iDistanceTerm = -1;
  char distanceOp = 0;
  int iKnnPredicateTerm = -1;
  int knnPredicatePlan = -1;
  // data-column pushdown candidates (decision 10), in constraint order
  int nPushdown = 0;
  int pushdownTerm[TG0_MAX_PUSHDOWN_BLOCKS];
  int pushdownDataIdx[TG0_MAX_PUSHDOWN_BLOCKS];
  char pushdownOp[TG0_MAX_PUSHDOWN_BLOCKS];

  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    int iColumn = pIdxInfo->aConstraint[i].iColumn;
    int op = pIdxInfo->aConstraint[i].op;

    if (!pIdxInfo->aConstraint[i].usable) {
      // a MATCH whose right-hand side is not available in this join order
      // (an outer-loop column, say) can only be satisfied by the KNN plan,
      // so this ordering is not viable — SQLITE_CONSTRAINT asks the planner
      // to try another rather than failing the statement
      if (op == SQLITE_INDEX_CONSTRAINT_MATCH) {
        hasUnusableMatch = 1;
      }
      continue;
    }

    // `geomcol MATCH <point>`: the KNN plan (plan '4'). Exactly one per
    // scan, and only on a geometry column
    if (op == SQLITE_INDEX_CONSTRAINT_MATCH) {
      tg0_user_column_kind kind;
      int idx;
      if (tg0_column_lookup(p, iColumn, &kind, &idx) != SQLITE_OK ||
          kind != TG0_USER_COLUMN_KIND_GEOMETRY) {
        tg_vtab_set_error(pVtab, "MATCH is only supported on tg0 geometry "
                                 "columns (nearest-neighbor queries)");
        return SQLITE_ERROR;
      }
      if (iKnnTerm >= 0) {
        tg_vtab_set_error(pVtab, "only one MATCH constraint is supported per "
                                 "tg0 query");
        return SQLITE_ERROR;
      }
      iKnnTerm = i;
      knnGeomIdx = idx;
      continue;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
      // a LIMIT is only a row-count hint: the KNN scan streams nearest-first
      // and never truncates, so correctness never depends on it
      sqlite3_value *value = NULL;
      if (sqlite3_libversion_number() >= 3038000 &&
          sqlite3_vtab_rhs_value(pIdxInfo, i, &value) == SQLITE_OK && value &&
          sqlite3_value_type(value) == SQLITE_INTEGER) {
        limit = sqlite3_value_int64(value);
      }
      continue;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_OFFSET) {
      continue;
    }
    // constraints on the hidden distance column are only meaningful in a
    // KNN scan, where SQLite evaluates them against xColumn's distance
    // (ticket 04 folds `distance <= ?` into the traversal as a prune).
    // Without a MATCH the column is NULL and the query cannot mean anything
    if (iColumn == tg0_column_distance_idx(p)) {
      if (iDistanceTerm < 0 && (op == SQLITE_INDEX_CONSTRAINT_LT ||
                                op == SQLITE_INDEX_CONSTRAINT_LE)) {
        iDistanceTerm = i;
        distanceOp = op == SQLITE_INDEX_CONSTRAINT_LT ? TG0_PUSHDOWN_OP_LT
                                                       : TG0_PUSHDOWN_OP_LE;
      }
      continue;
    }

    // rowid EQ — or primary key EQ, the declared primary key is always
    // column 0 — becomes a single content-table point lookup
    if (op == SQLITE_INDEX_CONSTRAINT_EQ &&
        (iColumn < 0 || (p->pkColumnName && iColumn == 0))) {
      if (iPointTerm < 0) {
        iPointTerm = i;
      }
      continue;
    }

    // EQ/NE/LT/LE/GT/GE on a plain data column: a pushdown candidate,
    // folded into the content scan's WHERE clause as a prefilter when a
    // fullscan or predicate plan is chosen (decision 10). Constraints on
    // the primary key or on geometry columns are never pushed
    // (tg0_column_lookup rejects the pk / distance columns, and the kind
    // check rejects geometry columns)
    char pushOpChar = tg0_pushdown_op_char(op);
    if (pushOpChar) {
      tg0_user_column_kind kind;
      int idx;
      if (tg0_column_lookup(p, iColumn, &kind, &idx) == SQLITE_OK &&
          kind == TG0_USER_COLUMN_KIND_DATA &&
          nPushdown < TG0_MAX_PUSHDOWN_BLOCKS) {
        // TEXT columns: the shadow-table prefilter always compares with
        // BINARY collation (untyped shadow columns, no COLLATE clause), so
        // only consume the constraint when SQLite itself would compare with
        // BINARY. Under NOCASE etc. a prefilter miss would be a false
        // negative that the omit=0 re-check cannot repair — re-checking
        // only ever removes rows
        if (p->data_columns[idx].type == SQLITE_TEXT) {
          const char *collation = sqlite3_vtab_collation(pIdxInfo, i);
          if (!collation || sqlite3_stricmp(collation, "BINARY") != 0) {
            continue;
          }
        }
        pushdownTerm[nPushdown] = i;
        pushdownDataIdx[nPushdown] = idx;
        pushdownOp[nPushdown] = pushOpChar;
        nPushdown++;
      }
      continue;
    }

    int iPlan = tg0_predicate_plan_by_op(op);
    if (iPlan >= 0) {
      tg0_user_column_kind kind;
      int idx;
      // only intercept a predicate whose first argument is one of this
      // table's geometry columns. Predicates on the primary key or on data
      // columns are left unconsumed, and run as ordinary scalar functions
      // over the values xColumn returns — exactly like outside a tg0 WHERE
      // clause (fixes the old aux-interception bug)
      if (tg0_column_lookup(p, iColumn, &kind, &idx) != SQLITE_OK ||
          kind != TG0_USER_COLUMN_KIND_GEOMETRY) {
        continue;
      }
      // consume exactly one predicate for the rtree plan, preferring the
      // first non-disjoint one: disjoint's matches live outside the query
      // bbox, so its prefilter is a fullscan. Every other predicate stays
      // unconsumed and SQLite post-evaluates it as a scalar function over
      // the returned WKB — multiple predicates in one WHERE clause simply
      // intersect (decision 8)
      if (iPredicateTerm < 0 ||
          (tg0_predicate_plans[predicatePlan].strategy ==
               TG0_STRATEGY_FULLSCAN &&
           tg0_predicate_plans[iPlan].strategy != TG0_STRATEGY_FULLSCAN)) {
        iPredicateTerm = i;
        predicatePlan = iPlan;
        predicateGeomIdx = idx;
      }
    }
  }

  // KNN: fold one predicate on the MATCH column into the walk (decision
  // 10), preferring one whose rtree strategy prunes cells. Predicates on
  // other geometry columns, and any further ones on this column, stay
  // unconsumed for SQLite to post-filter on the stream
  if (iKnnTerm >= 0) {
    for (int i = 0; i < pIdxInfo->nConstraint; i++) {
      if (!pIdxInfo->aConstraint[i].usable) {
        continue;
      }
      int iPlan = tg0_predicate_plan_by_op(pIdxInfo->aConstraint[i].op);
      tg0_user_column_kind kind;
      int idx;
      if (iPlan < 0 ||
          tg0_column_lookup(p, pIdxInfo->aConstraint[i].iColumn, &kind,
                            &idx) != SQLITE_OK ||
          kind != TG0_USER_COLUMN_KIND_GEOMETRY || idx != knnGeomIdx) {
        continue;
      }
      if (iKnnPredicateTerm < 0 ||
          (tg0_predicate_plans[knnPredicatePlan].strategy ==
               TG0_STRATEGY_FULLSCAN &&
           tg0_predicate_plans[iPlan].strategy != TG0_STRATEGY_FULLSCAN)) {
        iKnnPredicateTerm = i;
        knnPredicatePlan = iPlan;
      }
    }
  }

  char *idxStr;
  if (iKnnTerm < 0 && hasUnusableMatch) {
    return SQLITE_CONSTRAINT;
  }
  if (iKnnTerm < 0) {
    // a distance constraint outside a KNN scan: distance is NULL there, so
    // the query cannot mean what was intended — say so instead of returning
    // nothing
    for (int i = 0; i < pIdxInfo->nConstraint; i++) {
      if (pIdxInfo->aConstraint[i].usable &&
          pIdxInfo->aConstraint[i].iColumn == tg0_column_distance_idx(p) &&
          pIdxInfo->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_LIMIT &&
          pIdxInfo->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_OFFSET) {
        tg_vtab_set_error(pVtab,
                          "the tg0 distance column can only be used in a "
                          "nearest-neighbor query (geomcol MATCH point)");
        return SQLITE_ERROR;
      }
    }
  }
  if (iKnnTerm >= 0) {
    // KNN plan: the MATCH value is argv[0]. Every other constraint stays
    // unconsumed — predicates run as scalar functions over the returned
    // WKB, rowid/data-column/distance comparisons are evaluated by SQLite
    // against xColumn — which keeps the nearest-first stream exact. The
    // planner's own ORDER BY is consumed when it is exactly `distance ASC`,
    // the order the scan produces; anything else is sorted by SQLite
    sqlite3_str *str = sqlite3_str_new(NULL);
    int argvIndex = 1;
    sqlite3_str_appendf(str, "%c%c%c%c%c", TG0_PLAN_KNN, TG0_IDXSTR_KIND_KNN,
                        'A' + knnGeomIdx,
                        p->geometry_columns[knnGeomIdx].metric ==
                                TG_METRIC_PLANAR
                            ? 'P'
                            : 'G',
                        '_');
    pIdxInfo->aConstraintUsage[iKnnTerm].argvIndex = argvIndex++;
    pIdxInfo->aConstraintUsage[iKnnTerm].omit = 1;
    if (iKnnPredicateTerm >= 0) {
      sqlite3_str_appendf(str, "%c%c%c%c", TG0_IDXSTR_KIND_PREDICATE,
                          'A' + knnGeomIdx,
                          tg0_predicate_plans[knnPredicatePlan].opChar,
                          tg0_predicate_plans[knnPredicatePlan].strategy);
      pIdxInfo->aConstraintUsage[iKnnPredicateTerm].argvIndex = argvIndex++;
      pIdxInfo->aConstraintUsage[iKnnPredicateTerm].omit = 1;
    }
    if (iDistanceTerm >= 0) {
      sqlite3_str_appendf(str, "%c%c%c%c", TG0_IDXSTR_KIND_DISTANCE,
                          distanceOp, '_', '_');
      pIdxInfo->aConstraintUsage[iDistanceTerm].argvIndex = argvIndex++;
      // omit stays 0: the bound only ends the walk early, SQLite still
      // compares the exact distance
    }
    for (int k = 0; k < nPushdown; k++) {
      sqlite3_str_appendf(str, "%c%c%c%c", TG0_IDXSTR_KIND_PUSHDOWN,
                          'A' + pushdownDataIdx[k], pushdownOp[k], '_');
      pIdxInfo->aConstraintUsage[pushdownTerm[k]].argvIndex = argvIndex++;
    }
    idxStr = sqlite3_str_finish(str);
    if (pIdxInfo->nOrderBy == 1 &&
        pIdxInfo->aOrderBy[0].iColumn == tg0_column_distance_idx(p) &&
        !pIdxInfo->aOrderBy[0].desc) {
      pIdxInfo->orderByConsumed = 1;
    }
    // a best-first walk touches O(log n) nodes plus one content lookup per
    // emitted row; cheaper than any rtree prefilter plan for small k
    pIdxInfo->estimatedRows = limit > 0 ? limit : 25;
    pIdxInfo->estimatedCost = 10.0 + (double)pIdxInfo->estimatedRows;
  } else if (iPointTerm >= 0) {
    // point plans consume no pushdown blocks: the scan is already a single
    // B-tree lookup, and SQLite evaluates the data-column constraints on
    // the one returned row
    idxStr = sqlite3_mprintf("%c", TG0_PLAN_POINT);
    pIdxInfo->aConstraintUsage[iPointTerm].argvIndex = 1;
    pIdxInfo->estimatedCost = 1.0;
    pIdxInfo->estimatedRows = 1;
  } else {
    // predicate and fullscan plans share the trailing '&' pushdown blocks:
    // argv slots are assigned in consumed order — the query geometry (when
    // present) stays argv[0], pushdown values follow
    sqlite3_str *str = sqlite3_str_new(NULL);
    int argvIndex = 1;
    if (iPredicateTerm >= 0) {
      sqlite3_str_appendf(str, "%c%c%c%c%c", TG0_PLAN_PREDICATE,
                          TG0_IDXSTR_KIND_PREDICATE, 'A' + predicateGeomIdx,
                          tg0_predicate_plans[predicatePlan].opChar,
                          tg0_predicate_plans[predicatePlan].strategy);
      pIdxInfo->aConstraintUsage[iPredicateTerm].argvIndex = argvIndex++;
      pIdxInfo->aConstraintUsage[iPredicateTerm].omit = 1;
      pIdxInfo->estimatedCost = 30.0;
      pIdxInfo->estimatedRows = 10;
    } else {
      sqlite3_str_appendf(str, "%c", TG0_PLAN_FULLSCAN);
      pIdxInfo->estimatedCost = 3000000.0;
      pIdxInfo->estimatedRows = 100000;
    }
    for (int k = 0; k < nPushdown; k++) {
      sqlite3_str_appendf(str, "%c%c%c%c", TG0_IDXSTR_KIND_PUSHDOWN,
                          'A' + pushdownDataIdx[k], pushdownOp[k], '_');
      pIdxInfo->aConstraintUsage[pushdownTerm[k]].argvIndex = argvIndex++;
      // omit stays 0 on pushdown terms: SQLite re-evaluates the original
      // comparison against xColumn's values with its own affinity and
      // collation semantics, so the shadow-table prefilter only ever
      // removes rows. Correctness then only needs the prefilter to avoid
      // false negatives (tg0Filter documents how); everything subtler stays
      // exactly SQLite's, and a prefilter clause can be skipped freely
      pIdxInfo->estimatedCost *= 0.9;
    }
    idxStr = sqlite3_str_finish(str);
  }
  if (!idxStr) {
    return SQLITE_NOMEM;
  }
  pIdxInfo->idxStr = idxStr;
  pIdxInfo->needToFreeIdxStr = 1;

  return SQLITE_OK;
}

// Number of leading header columns (c.rowid, then r.id on text-primary-key
// tables) in a tg0_cursor's content scan, before the geomNN/dataNN columns.
static int tg0_scan_header_columns(tg0_vtab *p) {
  return p->pkColumnType == SQLITE_TEXT ? 2 : 1;
}

// ---------------------------------------------------------------------------
// tg0 KNN plan ('4'): best-first traversal of a geometry column's rtree
// (plans/knn/plan.md decisions 2, 8, 9).
//
// `geomcol MATCH <point>` puts a scan into nearest-neighbor mode: rows come
// out in ascending distance from the query point under the column's
// metric=, streamed with no top-k truncation, so LIMIT and any extra WHERE
// terms SQLite post-filters stay correct. The hidden `distance` column
// carries each row's exact distance.
//
// SQLite's own sqlite3_rtree_query_callback() implements exactly this
// search, but it is not part of the loadable-extension API (sqlite3ext.h
// has no rtree entries), so tg0 walks the rtree's %_node shadow blobs
// itself. The format is documented in rtree.c's "Database Format of R-Tree
// Tables": node 1 is the root and always exists; bytes 0-1 of the root hold
// the tree depth (big-endian), bytes 2-3 of every node its cell count; each
// cell is an 8-byte big-endian integer (leaf level: rowid; otherwise: child
// nodeno) followed by 4 big-endian float32 coordinates minX, maxX, minY,
// maxY. Dirty nodes are flushed to %_node at the end of every rtree
// xUpdate, so a read inside the same transaction sees the current tree.
//
// One min-heap per cursor holds three entry kinds, keyed by a lower bound
// of the distance to everything beneath the entry:
//
//   NODE  nodeno + level   key = metric rect bound of the node's cell bbox
//   LEAF  rowid            key = metric rect bound of the row's stored bbox
//   ROW   rowid            key = the row's exact distance (already refined)
//
// Pop loop: NODE -> read the blob, push one entry per cell; LEAF -> fetch
// the content row, parse the WKB, compute the exact distance, keep a copy
// of the row's values and push ROW (or emit at once when nothing left can
// beat it); ROW -> emit the kept values. Each content row is fetched exactly
// once: on a table larger than the page cache a second fetch per row costs
// a page read, which dominated k=1000 scans of a 2 GB table.
// Stored bboxes are float32 rounded outward, so a cell bound never exceeds
// the true distance of any row beneath it; and a ROW key is exact, so a
// popped ROW is nearer than everything remaining. Ties break ROW < LEAF <
// NODE, then by id, so equal distances emit deterministically.
// ---------------------------------------------------------------------------

#define TG0_KNN_KIND_ROW 0
#define TG0_KNN_KIND_LEAF 1
#define TG0_KNN_KIND_NODE 2

// deepest rtree accepted before a node blob is declared corrupt (SQLite's
// own rtree refuses depths beyond RTREE_MAX_DEPTH == 40)
#define TG0_KNN_MAX_DEPTH 40
// bytes per cell in a 2-D float rtree node: int64 id + 4 float32 coords
#define TG0_RTREE_CELL_BYTES 24

// A refined candidate's content-row values (the full scan column layout:
// c.rowid, [r.id], geomNN..., dataNN...), copied out of the fetch statement
// so the row need not be fetched again when it is emitted.
typedef struct tg0_knn_row {
  int n;
  sqlite3_value *values[1];
} tg0_knn_row;

typedef struct tg0_knn_entry {
  double key;
  int kind;
  // NODE only: the node's level, 0 for a node whose cells are leaf entries
  int level;
  // NODE: nodeno; LEAF / ROW: rowid
  sqlite3_int64 id;
  // ROW only: the kept row values (owned by the entry until emitted)
  tg0_knn_row *row;
} tg0_knn_entry;

typedef struct tg0_knn {
  tg0_knn_entry *heap;
  int n;
  int cap;
  // SELECT data FROM t_rtreeNN_node WHERE nodeno = ?1
  sqlite3_stmt *nodeStmt;
  // index into geometry_columns[] of the scanned column
  int geomIdx;
  tg_metric metric;
  struct tg_point q;
  // exact distance of the current (emitted) row, and its kept values —
  // xColumn/xRowid read from these on KNN plans
  double currentDistance;
  tg0_knn_row *current;
  // folded predicate ('P' block), or predFunc == NULL: the exact test
  // applied to every candidate, its rtree strategy letter used to prune
  // cells, and the query bbox grown outward to float32 like plan '3'
  GeomPredicateFunc predFunc;
  const struct tg_geom *predGeom;
  struct tg_geom *predGeomOwned;
  char predStrategy;
  struct tg_rect predRect;
  // distance bound ('D' block): the walk ends once the heap minimum is
  // >= maxDistance (maxStrict) or > maxDistance; INFINITY when absent
  double maxDistance;
  int maxStrict;
  // 1 when '&' pushdown clauses were applied to the content fetch: a
  // candidate rowid the fetch does not return was filtered, not lost
  int hasPushdown;
} tg0_knn;

static int tg0_knn_entry_less(const tg0_knn_entry *a, const tg0_knn_entry *b) {
  if (a->key != b->key) {
    return a->key < b->key;
  }
  if (a->kind != b->kind) {
    return a->kind < b->kind;
  }
  return a->id < b->id;
}

static int tg0_knn_push(tg0_knn *knn, tg0_knn_entry entry) {
  if (knn->n == knn->cap) {
    int cap = knn->cap ? knn->cap * 2 : 64;
    tg0_knn_entry *heap =
        sqlite3_realloc64(knn->heap, (sqlite3_uint64)cap * sizeof(*heap));
    if (!heap) {
      return SQLITE_NOMEM;
    }
    knn->heap = heap;
    knn->cap = cap;
  }
  int i = knn->n++;
  knn->heap[i] = entry;
  while (i > 0) {
    int parent = (i - 1) / 2;
    if (!tg0_knn_entry_less(&knn->heap[i], &knn->heap[parent])) {
      break;
    }
    tg0_knn_entry tmp = knn->heap[i];
    knn->heap[i] = knn->heap[parent];
    knn->heap[parent] = tmp;
    i = parent;
  }
  return SQLITE_OK;
}

// Pops the minimum entry into *out. Returns 0 when the heap is empty.
static int tg0_knn_pop(tg0_knn *knn, tg0_knn_entry *out) {
  if (knn->n == 0) {
    return 0;
  }
  *out = knn->heap[0];
  knn->n--;
  if (knn->n == 0) {
    return 1;
  }
  knn->heap[0] = knn->heap[knn->n];
  int i = 0;
  while (1) {
    int left = 2 * i + 1;
    int right = left + 1;
    int smallest = i;
    if (left < knn->n &&
        tg0_knn_entry_less(&knn->heap[left], &knn->heap[smallest])) {
      smallest = left;
    }
    if (right < knn->n &&
        tg0_knn_entry_less(&knn->heap[right], &knn->heap[smallest])) {
      smallest = right;
    }
    if (smallest == i) {
      break;
    }
    tg0_knn_entry tmp = knn->heap[i];
    knn->heap[i] = knn->heap[smallest];
    knn->heap[smallest] = tmp;
    i = smallest;
  }
  return 1;
}

static void tg0_knn_row_free(tg0_knn_row *row) {
  if (!row) {
    return;
  }
  for (int i = 0; i < row->n; i++) {
    sqlite3_value_free(row->values[i]);
  }
  sqlite3_free(row);
}

// Copies every column of the positioned fetch statement into a new row.
static tg0_knn_row *tg0_knn_row_dup(sqlite3_stmt *stmt) {
  int n = sqlite3_column_count(stmt);
  tg0_knn_row *row = sqlite3_malloc64(sizeof(*row) + (sqlite3_uint64)n * sizeof(sqlite3_value *));
  if (!row) {
    return NULL;
  }
  row->n = n;
  for (int i = 0; i < n; i++) {
    row->values[i] = sqlite3_value_dup(sqlite3_column_value(stmt, i));
    if (!row->values[i]) {
      row->n = i;
      tg0_knn_row_free(row);
      return NULL;
    }
  }
  return row;
}

static void tg0_knn_free(tg0_knn *knn) {
  if (!knn) {
    return;
  }
  for (int i = 0; i < knn->n; i++) {
    tg0_knn_row_free(knn->heap[i].row);
  }
  tg0_knn_row_free(knn->current);
  sqlite3_free(knn->heap);
  sqlite3_finalize(knn->nodeStmt);
  tg_geom_free(knn->predGeomOwned);
  sqlite3_free(knn);
}

static unsigned tg0_read_be16(const unsigned char *p) {
  return ((unsigned)p[0] << 8) | p[1];
}

static sqlite3_int64 tg0_read_be64(const unsigned char *p) {
  sqlite3_uint64 v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 8) | p[i];
  }
  return (sqlite3_int64)v;
}

static double tg0_read_be_f32(const unsigned char *p) {
  unsigned int bits = ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
                      ((unsigned int)p[2] << 8) | p[3];
  float f;
  memcpy(&f, &bits, sizeof(f));
  return (double)f;
}

// Reads rtree node nodeno and pushes one heap entry per cell. level is the
// node's own level; -1 means "the root, read the depth from its header".
// Malformed blobs are reported as SQLITE_CORRUPT_VTAB.
static int tg0_knn_expand_node(tg0_vtab *p, tg0_knn *knn, sqlite3_int64 nodeno,
                               int level) {
  sqlite3_stmt *stmt = knn->nodeStmt;
  int rc;
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, nodeno);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      tg_vtab_set_error(&p->base,
                        "tg0 table %s: rtree node %lld is missing from %s_rtree%02d_node",
                        p->tableName, nodeno, p->tableName, knn->geomIdx);
      return SQLITE_CORRUPT_VTAB;
    }
    tg_vtab_set_error(&p->base, "Error reading tg0 rtree node: %s",
                      sqlite3_errmsg(p->db));
    return rc;
  }
  const unsigned char *data = sqlite3_column_blob(stmt, 0);
  int nData = sqlite3_column_bytes(stmt, 0);
  if (!data || nData < 4) {
    goto corrupt;
  }
  if (level < 0) {
    level = (int)tg0_read_be16(data);
    if (level > TG0_KNN_MAX_DEPTH) {
      goto corrupt;
    }
  }
  int nCell = (int)tg0_read_be16(data + 2);
  if (4 + (sqlite3_int64)nCell * TG0_RTREE_CELL_BYTES > nData) {
    goto corrupt;
  }
  for (int i = 0; i < nCell; i++) {
    const unsigned char *cell = data + 4 + i * TG0_RTREE_CELL_BYTES;
    struct tg_rect rect = {{tg0_read_be_f32(cell + 8), tg0_read_be_f32(cell + 16)},
                           {tg0_read_be_f32(cell + 12), tg0_read_be_f32(cell + 20)}};
    if (knn->predFunc) {
      // cell prune by the folded predicate's rtree strategy. Child bboxes
      // lie inside their node's bbox, so a node that cannot overlap (or
      // contain) the query bbox has no matching rows beneath it; the
      // within-style strategies can only be applied exactly at the leaves
      struct tg_rect qr = knn->predRect;
      int overlap = rect.min.x <= qr.max.x && rect.max.x >= qr.min.x &&
                    rect.min.y <= qr.max.y && rect.max.y >= qr.min.y;
      int contains = rect.min.x <= qr.min.x && rect.max.x >= qr.max.x &&
                     rect.min.y <= qr.min.y && rect.max.y >= qr.max.y;
      int within = rect.min.x >= qr.min.x && rect.max.x <= qr.max.x &&
                   rect.min.y >= qr.min.y && rect.max.y <= qr.max.y;
      switch (knn->predStrategy) {
      case TG0_STRATEGY_OVERLAP:
        if (!overlap) {
          continue;
        }
        break;
      case TG0_STRATEGY_CONTAINS:
        if (!contains) {
          continue;
        }
        break;
      case TG0_STRATEGY_WITHIN:
        if (level == 0 ? !within : !overlap) {
          continue;
        }
        break;
      default:
        break;
      }
    }
    tg0_knn_entry entry;
    entry.row = NULL;
    entry.id = tg0_read_be64(cell);
    if (level > 0 && entry.id <= 1) {
      // a child pointer back to the root (or to nothing) can only come from
      // a corrupt node; the level count already bounds the walk's depth
      goto corrupt;
    }
    entry.key = tg_metric_point_rect(knn->metric, knn->q, rect);
    if (isnan(entry.key)) {
      // a NaN bound would break heap ordering; treat as unreachable
      entry.key = INFINITY;
    }
    if (level == 0) {
      entry.kind = TG0_KNN_KIND_LEAF;
      entry.level = 0;
    } else {
      entry.kind = TG0_KNN_KIND_NODE;
      entry.level = level - 1;
    }
    rc = tg0_knn_push(knn, entry);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  return SQLITE_OK;

corrupt:
  tg_vtab_set_error(&p->base,
                    "tg0 table %s: malformed rtree node %lld in %s_rtree%02d_node",
                    p->tableName, nodeno, p->tableName, knn->geomIdx);
  return SQLITE_CORRUPT_VTAB;
}

// Positions pCur->stmt (the by-rowid content fetch) on rowid. Returns
// SQLITE_DONE when the fetch returns no row and allowMissing is set (a
// pushdown clause rejected the candidate); otherwise a missing row means the
// rtree and content table disagree.
static int tg0_knn_fetch_row(tg0_cursor *pCur, sqlite3_int64 rowid,
                             int allowMissing) {
  tg0_vtab *p = (tg0_vtab *)pCur->base.pVtab;
  int rc;
  sqlite3_reset(pCur->stmt);
  sqlite3_bind_int64(pCur->stmt, 1, rowid);
  rc = sqlite3_step(pCur->stmt);
  if (rc == SQLITE_ROW) {
    return SQLITE_OK;
  }
  if (rc == SQLITE_DONE && allowMissing) {
    return SQLITE_DONE;
  }
  if (rc == SQLITE_DONE) {
    tg_vtab_set_error(&p->base,
                      "tg0 table %s: rtree entry %lld has no content row",
                      p->tableName, rowid);
    return SQLITE_CORRUPT_VTAB;
  }
  tg_vtab_set_error(&p->base, "Error fetching tg0 content row: %s",
                    sqlite3_errmsg(p->db));
  return rc;
}

// Advances a KNN scan to its next row (positioned in pCur->stmt with its
// distance in knn->currentDistance), or sets eof.
static int tg0_knn_step(tg0_cursor *pCur) {
  tg0_vtab *p = (tg0_vtab *)pCur->base.pVtab;
  tg0_knn *knn = pCur->knn;
  tg0_knn_entry entry;
  int rc;
  tg0_knn_row_free(knn->current);
  knn->current = NULL;
  while (tg0_knn_pop(knn, &entry)) {
    // the heap minimum bounds everything left: past the distance bound the
    // scan is over
    if (knn->maxStrict ? entry.key >= knn->maxDistance
                       : entry.key > knn->maxDistance) {
      tg0_knn_row_free(entry.row);
      break;
    }
    if (entry.kind == TG0_KNN_KIND_NODE) {
      rc = tg0_knn_expand_node(p, knn, entry.id, entry.level);
      if (rc != SQLITE_OK) {
        return rc;
      }
      continue;
    }
    if (entry.kind == TG0_KNN_KIND_ROW) {
      knn->current = entry.row;
      knn->currentDistance = entry.key;
      return SQLITE_OK;
    }
    // LEAF: fetch once (a candidate may be rejected by the pushdown
    // prefilter) and refine the bound into the exact distance
    rc = tg0_knn_fetch_row(pCur, entry.id, knn->hasPushdown);
    if (rc == SQLITE_DONE) {
      continue;
    }
    if (rc != SQLITE_OK) {
      return rc;
    }
    int iScan = tg0_scan_header_columns(p) + knn->geomIdx;
    const void *wkb = sqlite3_column_blob(pCur->stmt, iScan);
    int nWkb = sqlite3_column_bytes(pCur->stmt, iScan);
    struct tg_geom *candidate =
        tg_parse_wkb_ix(wkb, nWkb, p->geometry_columns[knn->geomIdx].indexing);
    if (tg_geom_error(candidate)) {
      tg_vtab_set_error(&p->base, "Error parsing tg0 stored geometry in %s.%s: %s",
                        p->tableName, p->geometry_columns[knn->geomIdx].name,
                        tg_geom_error(candidate));
      tg_geom_free(candidate);
      return SQLITE_ERROR;
    }
    if (knn->predFunc && !knn->predFunc(candidate, knn->predGeom)) {
      tg_geom_free(candidate);
      continue;
    }
    double exact;
    bool ok = tg_metric_point_geom(knn->metric, knn->q, candidate, &exact);
    tg_geom_free(candidate);
    if (!ok) {
      return SQLITE_NOMEM;
    }
    if (isinf(exact) || isnan(exact)) {
      // empty geometry: no nearest point, never emitted (decision 6)
      continue;
    }
    tg0_knn_row *row = tg0_knn_row_dup(pCur->stmt);
    if (!row) {
      return SQLITE_NOMEM;
    }
    if (knn->n == 0 || exact < knn->heap[0].key) {
      // nothing left can be nearer: emit right away
      knn->current = row;
      knn->currentDistance = exact;
      return SQLITE_OK;
    }
    entry.kind = TG0_KNN_KIND_ROW;
    entry.key = exact;
    entry.row = row;
    rc = tg0_knn_push(knn, entry);
    if (rc != SQLITE_OK) {
      tg0_knn_row_free(row);
      return rc;
    }
  }
  pCur->eof = 1;
  return SQLITE_OK;
}

// ---------------------------------------------------------------------------
// tg0 read path.
//
// Every plan scans t_content (joined with t_rowids on text-primary-key
// tables) through the same SELECT shape; the plans differ only in how the
// candidate rows are narrowed:
//
//   fullscan  ('1'): iterate t_content directly.
//   point     ('2'): WHERE c.rowid = ?1 (or r.id = ?1 for text pks).
//   predicate ('3'): join the geometry column's rtree on a bbox condition
//                    per the strategy letter, then apply the exact tg
//                    predicate to each candidate row in tg0_cursor_step —
//                    the rtree is only ever a prefilter, correctness comes
//                    from the exact test.
//   knn       ('4'): WHERE c.rowid = ?1, driven row by row from the
//                    best-first rtree walk in tg0_knn_step.
// ---------------------------------------------------------------------------

// Round v to the nearest float32-representable double at or below (down) /
// at or above (up) v. Used to grow query bboxes outward before binding them
// against rtree coordinates, mirroring the outward float32 rounding the
// rtree module applies to stored bboxes.
static double tg0_f32_down(double v) {
  float f = (float)v;
  if ((double)f > v) {
    f = nextafterf(f, -INFINITY);
  }
  return (double)f;
}
static double tg0_f32_up(double v) {
  float f = (float)v;
  if ((double)f < v) {
    f = nextafterf(f, INFINITY);
  }
  return (double)f;
}

// Advances the cursor's scan to the next result row, setting eof at the end.
// On predicate plans this steps until a candidate passes the exact tg
// predicate: the candidate's stored WKB is parsed with the geometry column's
// indexing= option (decision 7) and tested as func(candidate, query) —
// tg_predicate_impl's argument order for tg_pred(column-value, argument).
// A step or parse failure returns its error with eof still unset, so a
// mid-scan failure never reads as a clean end of results.
static int tg0_cursor_step(tg0_cursor *pCur) {
  tg0_vtab *p = (tg0_vtab *)pCur->base.pVtab;
  while (1) {
    int rc = sqlite3_step(pCur->stmt);
    if (rc == SQLITE_DONE) {
      pCur->eof = 1;
      return SQLITE_OK;
    }
    if (rc != SQLITE_ROW) {
      tg_vtab_set_error(&p->base, "Error stepping tg0 content rows: %s",
                        sqlite3_errmsg(p->db));
      return rc;
    }
    if (pCur->plan != TG0_PLAN_PREDICATE) {
      return SQLITE_OK;
    }
    // exact post-filter for predicate plans
    int iScan = tg0_scan_header_columns(p) + pCur->predGeomIdx;
    const void *wkb = sqlite3_column_blob(pCur->stmt, iScan);
    int nWkb = sqlite3_column_bytes(pCur->stmt, iScan);
    struct tg_geom *candidate = tg_parse_wkb_ix(
        wkb, nWkb, p->geometry_columns[pCur->predGeomIdx].indexing);
    if (tg_geom_error(candidate)) {
      tg_vtab_set_error(&p->base, "Error parsing tg0 stored geometry in %s.%s: %s",
                        p->tableName,
                        p->geometry_columns[pCur->predGeomIdx].name,
                        tg_geom_error(candidate));
      tg_geom_free(candidate);
      return SQLITE_ERROR;
    }
    bool matches = pCur->predFunc(candidate, pCur->queryGeom);
    tg_geom_free(candidate);
    if (matches) {
      return SQLITE_OK;
    }
  }
}

static int tg0Filter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                     const char *idxStr, int argc, sqlite3_value **argv) {
  tg0_cursor *pCur = (tg0_cursor *)pVtabCursor;
  tg0_vtab *p = (tg0_vtab *)pVtabCursor->pVtab;
  char strategy = 0;
  struct tg_rect queryRect = {{0, 0}, {0, 0}};
  int rc;
  // parsed '&' pushdown blocks: data_columns[] index, [cmp-op] letter, and
  // whether the block's prefilter clause is applied to this scan. Block k
  // describes argv[argvOffset + k] and binds parameter ?(bindBase + k)
  int nPushdown = 0;
  int pushdownDataIdx[TG0_MAX_PUSHDOWN_BLOCKS];
  char pushdownOp[TG0_MAX_PUSHDOWN_BLOCKS];
  int pushdownInclude[TG0_MAX_PUSHDOWN_BLOCKS];
  int argvOffset = 0;
  const char *knnPushdownBlocks = "";
  // pushdown parameter numbers start after the prefilter's own ?1..?4
  // (point key / rtree bbox); unreferenced lower numbers are harmless
  int bindBase = 5;

  // cursors are reused: tear down the previous scan's state entirely
  sqlite3_finalize(pCur->stmt);
  pCur->stmt = NULL;
  tg_geom_free(pCur->queryGeom);
  pCur->queryGeom = NULL;
  tg0_knn_free(pCur->knn);
  pCur->knn = NULL;
  pCur->predFunc = NULL;
  pCur->predGeomIdx = -1;
  pCur->eof = 0;
  pCur->plan = idxStr[0];

  if (pCur->plan == TG0_PLAN_KNN) {
    // idxStr = '4' + 'K' [geom-col] [metric] '_', argv[0] = the MATCH value
    int geomIdx = strlen(idxStr) >= 5 ? idxStr[2] - 'A' : -1;
    if (strlen(idxStr) < 5 || idxStr[1] != TG0_IDXSTR_KIND_KNN || geomIdx < 0 ||
        geomIdx >= p->numGeometryColumns || argc < 1) {
      tg_vtab_set_error(&p->base, "Internal tg0 error: bad KNN plan '%s'",
                        idxStr);
      return SQLITE_ERROR;
    }
    struct tg0_geometry_column *column = &p->geometry_columns[geomIdx];
    char *errmsg = NULL;
    if (geomValue(argv[0], &pCur->queryGeom, &errmsg) != SQLITE_OK) {
      tg_vtab_set_error(&p->base, "%s.%s MATCH: %s", p->tableName,
                        column->name, errmsg);
      sqlite3_free(errmsg);
      return SQLITE_ERROR;
    }
    if (tg_geom_typeof(pCur->queryGeom) != TG_POINT ||
        tg_geom_is_empty(pCur->queryGeom)) {
      tg_vtab_set_error(&p->base,
                        "%s.%s MATCH: nearest-neighbor queries take a Point, "
                        "received %s%s",
                        p->tableName, column->name,
                        tg_geom_is_empty(pCur->queryGeom) ? "an empty " : "a ",
                        tg_geom_type_string(tg_geom_typeof(pCur->queryGeom)));
      return SQLITE_ERROR;
    }
    struct tg_point q = tg_geom_point(pCur->queryGeom);
    if (column->metric == TG_METRIC_GEODESIC ? !tg_lonlat_valid(q.x, q.y)
                                             : !(isfinite(q.x) && isfinite(q.y))) {
      tg_vtab_set_error(&p->base,
                        column->metric == TG_METRIC_GEODESIC
                            ? "%s.%s MATCH: query point (%g, %g) is outside "
                              "lon/lat range"
                            : "%s.%s MATCH: query point (%g, %g) must be finite",
                        p->tableName, column->name, q.x, q.y);
      return SQLITE_ERROR;
    }
    pCur->knn = sqlite3_malloc(sizeof(*pCur->knn));
    if (!pCur->knn) {
      return SQLITE_NOMEM;
    }
    memset(pCur->knn, 0, sizeof(*pCur->knn));
    pCur->knn->geomIdx = geomIdx;
    pCur->knn->metric = column->metric;
    pCur->knn->q = q;
    pCur->knn->maxDistance = INFINITY;
    char *zNodeSql = sqlite3_mprintf("SELECT data FROM "
                                     TG0_SHADOW_RTREE_NODE_NAME
                                     " WHERE nodeno = ?1",
                                     p->schemaName, p->tableName, geomIdx);
    if (!zNodeSql) {
      return SQLITE_NOMEM;
    }
    rc = sqlite3_prepare_v2(p->db, zNodeSql, -1, &pCur->knn->nodeStmt, NULL);
    sqlite3_free(zNodeSql);
    if (rc != SQLITE_OK) {
      tg_vtab_set_error(&p->base, "Error preparing tg0 rtree node read: %s",
                        sqlite3_errmsg(p->db));
      return SQLITE_ERROR;
    }
  }

  if (pCur->plan == TG0_PLAN_PREDICATE) {
    // idxStr = '3' + [kind][geom-col][op][strategy], argv[0] = query geometry
    int iPlan = strlen(idxStr) >= 5 ? tg0_predicate_plan_by_char(idxStr[3]) : -1;
    pCur->predGeomIdx = strlen(idxStr) >= 5 ? idxStr[2] - 'A' : -1;
    if (iPlan < 0 || idxStr[1] != TG0_IDXSTR_KIND_PREDICATE ||
        pCur->predGeomIdx < 0 || pCur->predGeomIdx >= p->numGeometryColumns ||
        argc < 1) {
      tg_vtab_set_error(&p->base, "Internal tg0 error: bad predicate plan '%s'",
                        idxStr);
      return SQLITE_ERROR;
    }
    pCur->predFunc = tg0_predicate_plans[iPlan].func;
    strategy = idxStr[4];

    char *errmsg = NULL;
    if (geomValue(argv[0], &pCur->queryGeom, &errmsg) != SQLITE_OK) {
      tg_vtab_set_error(&p->base, "%s", errmsg);
      sqlite3_free(errmsg);
      return SQLITE_ERROR;
    }
    queryRect = tg_geom_rect(pCur->queryGeom);
  }

  // KNN fold blocks: an optional 'P' predicate on the MATCH column and an
  // optional 'D' distance bound, each consuming the next argv slot
  if (pCur->plan == TG0_PLAN_KNN) {
    argvOffset = 1;
    const char *block = idxStr + 5;
    if (strlen(block) >= 4 && block[0] == TG0_IDXSTR_KIND_PREDICATE) {
      int iPlan = tg0_predicate_plan_by_char(block[2]);
      if (iPlan < 0 || block[1] - 'A' != pCur->knn->geomIdx ||
          argvOffset >= argc) {
        tg_vtab_set_error(&p->base,
                          "Internal tg0 error: bad KNN predicate block in '%s'",
                          idxStr);
        return SQLITE_ERROR;
      }
      char *errmsg = NULL;
      struct tg_geom *predGeom = NULL;
      if (geomValue(argv[argvOffset], &predGeom, &errmsg) != SQLITE_OK) {
        tg_vtab_set_error(&p->base, "%s", errmsg);
        sqlite3_free(errmsg);
        return SQLITE_ERROR;
      }
      // the folded predicate's query geometry rides along in
      // pCur->queryGeom's sibling slot: keep it on the knn state, freed
      // with it
      pCur->knn->predGeomOwned = predGeom;
      pCur->knn->predGeom = predGeom;
      pCur->knn->predFunc = tg0_predicate_plans[iPlan].func;
      pCur->knn->predStrategy = block[3];
      struct tg_rect r = tg_geom_rect(predGeom);
      pCur->knn->predRect = (struct tg_rect){
          {tg0_f32_down(r.min.x), tg0_f32_down(r.min.y)},
          {tg0_f32_up(r.max.x), tg0_f32_up(r.max.y)}};
      argvOffset++;
      block += 4;
    }
    if (strlen(block) >= 4 && block[0] == TG0_IDXSTR_KIND_DISTANCE) {
      if ((block[1] != TG0_PUSHDOWN_OP_LT && block[1] != TG0_PUSHDOWN_OP_LE) ||
          argvOffset >= argc) {
        tg_vtab_set_error(&p->base,
                          "Internal tg0 error: bad KNN distance block in '%s'",
                          idxStr);
        return SQLITE_ERROR;
      }
      // only a numeric bound can end the walk early; anything else is left
      // entirely to SQLite's own comparison (omit is 0)
      sqlite3_value *bound = argv[argvOffset];
      if (sqlite3_value_numeric_type(bound) == SQLITE_INTEGER ||
          sqlite3_value_numeric_type(bound) == SQLITE_FLOAT) {
        pCur->knn->maxDistance = sqlite3_value_double(bound);
        pCur->knn->maxStrict = block[1] == TG0_PUSHDOWN_OP_LT;
      }
      argvOffset++;
      block += 4;
    }
    knnPushdownBlocks = block;
  }

  // trailing '&' pushdown blocks (fullscan, predicate, and KNN plans)
  if (pCur->plan == TG0_PLAN_FULLSCAN || pCur->plan == TG0_PLAN_PREDICATE ||
      pCur->plan == TG0_PLAN_KNN) {
    const char *block;
    if (pCur->plan == TG0_PLAN_KNN) {
      block = knnPushdownBlocks;
    } else {
      argvOffset = pCur->plan == TG0_PLAN_PREDICATE ? 1 : 0;
      block = idxStr + (pCur->plan == TG0_PLAN_PREDICATE ? 5 : 1);
    }
    for (; *block; block += 4) {
      if (strlen(block) < 4 || block[0] != TG0_IDXSTR_KIND_PUSHDOWN ||
          block[1] - 'A' < 0 || block[1] - 'A' >= p->numDataColumns ||
          !tg0_pushdown_op_sql(block[2]) ||
          nPushdown >= TG0_MAX_PUSHDOWN_BLOCKS ||
          argvOffset + nPushdown >= argc) {
        tg_vtab_set_error(&p->base,
                          "Internal tg0 error: bad pushdown block in '%s'",
                          idxStr);
        return SQLITE_ERROR;
      }
      pushdownDataIdx[nPushdown] = block[1] - 'A';
      pushdownOp[nPushdown] = block[2];
      nPushdown++;
    }
  }

  // Decide, per pushdown block, whether its prefilter clause is applied.
  // xBestIndex left omit=0 on every pushed constraint, so SQLite re-checks
  // each one with its own comparison semantics: a skipped clause is always
  // correct, and an applied clause must only never exclude a row SQLite's
  // own check would keep (no false negatives).
  //
  // Affinity: the shadow data columns are declared typeless (no affinity),
  // so the prefilter compares by storage class under BINARY collation.
  // Writes are type-checked, so stored classes always match the declared
  // column type (integers widen to REAL in float columns at bind time), and
  // xColumn returns the shadow values verbatim — the values SQLite's own
  // check compares, after applying the vtab column's declared affinity to
  // the constraint value. Mirroring that here:
  //
  // - INTEGER/FLOAT columns: apply numeric affinity to the argv value
  //   (sqlite3_value_numeric_type) before binding — the same conversion
  //   SQLite's comparison applies, so e.g. `size = '5'` prefilters as the
  //   integer 5 and a non-numeric text RHS stays text (ordering above every
  //   stored numeric on both sides).
  // - TEXT columns: a numeric RHS would be text-ified by SQLite's
  //   TEXT-affinity comparison but compares as a bare storage-class
  //   mismatch in the shadow table (numerics order below text — false
  //   negatives for '<'/'<='). Skip the clause instead of reproducing
  //   SQLite's number->text rendering; text/blob/NULL RHS compare
  //   identically on both sides (xBestIndex already required BINARY
  //   collation for TEXT columns).
  // - BLOB columns: no affinity conversion on either side in either check;
  //   bind verbatim.
  // - NULL RHS: always included — `x <op> NULL` matches nothing in the
  //   shadow scan, exactly SQL's semantics.
  for (int k = 0; k < nPushdown; k++) {
    sqlite3_value *value = argv[argvOffset + k];
    int columnType = p->data_columns[pushdownDataIdx[k]].type;
    int valueType = sqlite3_value_type(value);
    pushdownInclude[k] = 1;
    if (columnType == SQLITE_INTEGER || columnType == SQLITE_FLOAT) {
      sqlite3_value_numeric_type(value);
    } else if (columnType == SQLITE_TEXT && (valueType == SQLITE_INTEGER ||
                                             valueType == SQLITE_FLOAT)) {
      pushdownInclude[k] = 0;
    }
  }

  sqlite3_str *str = sqlite3_str_new(NULL);
  sqlite3_str_appendall(str, "SELECT c.rowid");
  if (p->pkColumnType == SQLITE_TEXT) {
    sqlite3_str_appendall(str, ", r.id");
  }
  for (int i = 0; i < p->numGeometryColumns; i++) {
    sqlite3_str_appendf(str, ", c.geom%02d", i);
  }
  for (int i = 0; i < p->numDataColumns; i++) {
    sqlite3_str_appendf(str, ", c.data%02d", i);
  }
  sqlite3_str_appendf(str, " FROM " TG0_SHADOW_CONTENT_NAME " AS c",
                      p->schemaName, p->tableName);
  if (p->pkColumnType == SQLITE_TEXT) {
    sqlite3_str_appendf(str,
                        " JOIN " TG0_SHADOW_ROWIDS_NAME
                        " AS r ON r.rowid = c.rowid",
                        p->schemaName, p->tableName);
  }
  if (pCur->plan == TG0_PLAN_POINT) {
    sqlite3_str_appendall(str, p->pkColumnType == SQLITE_TEXT
                                   ? " WHERE r.id = ?1"
                                   : " WHERE c.rowid = ?1");
  } else if (pCur->plan == TG0_PLAN_KNN) {
    // the KNN walk fetches content rows one rowid at a time
    sqlite3_str_appendall(str, " WHERE c.rowid = ?1");
  } else if (pCur->plan == TG0_PLAN_PREDICATE &&
             strategy != TG0_STRATEGY_FULLSCAN) {
    // rtree bbox prefilter. Bind order: ?1=qminX ?2=qmaxX ?3=qminY ?4=qmaxY
    sqlite3_str_appendf(str,
                        " JOIN " TG0_SHADOW_RTREE_NAME " AS i ON i.id = c.rowid",
                        p->schemaName, p->tableName, pCur->predGeomIdx);
    switch (strategy) {
    case TG0_STRATEGY_OVERLAP:
      sqlite3_str_appendall(str, " WHERE i.minX <= ?2 AND i.maxX >= ?1"
                                 " AND i.minY <= ?4 AND i.maxY >= ?3");
      break;
    case TG0_STRATEGY_CONTAINS:
      sqlite3_str_appendall(str, " WHERE i.minX <= ?1 AND i.maxX >= ?2"
                                 " AND i.minY <= ?3 AND i.maxY >= ?4");
      break;
    case TG0_STRATEGY_WITHIN:
      sqlite3_str_appendall(str, " WHERE i.minX >= ?1 AND i.maxX <= ?2"
                                 " AND i.minY >= ?3 AND i.maxY <= ?4");
      break;
    }
  }
  // applied pushdown clauses. Fullscan plans and disjoint-strategy
  // predicate plans have no WHERE clause yet; the first applied clause
  // starts one
  int hasWhere = pCur->plan == TG0_PLAN_POINT || pCur->plan == TG0_PLAN_KNN ||
                 (pCur->plan == TG0_PLAN_PREDICATE &&
                  strategy != TG0_STRATEGY_FULLSCAN);
  for (int k = 0; k < nPushdown; k++) {
    if (!pushdownInclude[k]) {
      continue;
    }
    if (pCur->knn) {
      pCur->knn->hasPushdown = 1;
    }
    sqlite3_str_appendf(str, "%s c.data%02d %s ?%d",
                        hasWhere ? " AND" : " WHERE", pushdownDataIdx[k],
                        tg0_pushdown_op_sql(pushdownOp[k]), bindBase + k);
    hasWhere = 1;
  }
  char *zSql = sqlite3_str_finish(str);
  if (!zSql) {
    return SQLITE_NOMEM;
  }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pCur->stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "Error preparing tg0 content scan: %s",
                      sqlite3_errmsg(p->db));
    return SQLITE_ERROR;
  }
  rc = SQLITE_OK;
  if (pCur->plan == TG0_PLAN_POINT && argc > 0) {
    rc = sqlite3_bind_value(pCur->stmt, 1, argv[0]);
  } else if (pCur->plan == TG0_PLAN_PREDICATE &&
             strategy != TG0_STRATEGY_FULLSCAN) {
    // The rtree stores each bbox as float32 rounded *outward* (min
    // coordinates toward -inf, max toward +inf — the rtree module's own
    // rounding), so a stored bbox is a superset of the row's true float64
    // bbox. The query bbox is grown outward to float32 the same way before
    // binding (plan.md "Float32 caveat"):
    //
    // - WITHIN ('W', stored.min >= qmin AND stored.max <= qmax): required
    //   for correctness. A true match's stored.min can round below an exact
    //   qmin bind, which would false-negative it; binding
    //   f32down(qmin)/f32up(qmax) keeps every outward-rounded stored bbox
    //   inside the comparison.
    // - CONTAINS ('C', stored.min <= qmin AND stored.max >= qmax): safe and
    //   lossless. A true match already has stored.min <= true.min <= qmin,
    //   and because stored values are float32, `stored.min <= f32down(qmin)`
    //   admits exactly the same stored values as `stored.min <= qmin`
    //   (f32down(qmin) is the largest float32 <= qmin) — likewise for max.
    // - OVERLAP ('O'): grows the query bbox by under one float32 ulp per
    //   edge, a harmless over-approximation.
    //
    // Any over-included candidate is discarded by the exact predicate in
    // tg0_cursor_step, so outward rounding never changes results.
    sqlite3_bind_double(pCur->stmt, 1, tg0_f32_down(queryRect.min.x));
    sqlite3_bind_double(pCur->stmt, 2, tg0_f32_up(queryRect.max.x));
    sqlite3_bind_double(pCur->stmt, 3, tg0_f32_down(queryRect.min.y));
    rc = sqlite3_bind_double(pCur->stmt, 4, tg0_f32_up(queryRect.max.y));
  }
  for (int k = 0; k < nPushdown && rc == SQLITE_OK; k++) {
    if (pushdownInclude[k]) {
      rc = sqlite3_bind_value(pCur->stmt, bindBase + k, argv[argvOffset + k]);
    }
  }
  if (rc != SQLITE_OK) {
    // a failed bind (OOM copying an argv value) must error out: stepping the
    // half-bound statement would silently scan with NULL parameters
    tg_vtab_set_error(&p->base, "Error binding tg0 content scan: %s",
                      sqlite3_errmsg(p->db));
    return rc;
  }
  if (pCur->plan == TG0_PLAN_KNN) {
    // seed the walk with the root node's cells, then produce the first row
    rc = tg0_knn_expand_node(p, pCur->knn, 1, -1);
    if (rc != SQLITE_OK) {
      return rc;
    }
    return tg0_knn_step(pCur);
  }
  return tg0_cursor_step(pCur);
}

static int tg0Rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  if (pCur->plan == TG0_PLAN_KNN && pCur->knn && pCur->knn->current) {
    *pRowid = sqlite3_value_int64(pCur->knn->current->values[0]);
    return SQLITE_OK;
  }
  *pRowid = sqlite3_column_int64(pCur->stmt, 0);
  return SQLITE_OK;
}

static int tg0Next(sqlite3_vtab_cursor *cur) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  if (pCur->plan == TG0_PLAN_KNN) {
    return tg0_knn_step(pCur);
  }
  return tg0_cursor_step(pCur);
}

static int tg0Eof(sqlite3_vtab_cursor *cur) { return ((tg0_cursor *)cur)->eof; }

static int tg0Column(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                     int i) {
  tg0_cursor *pCur = (tg0_cursor *)cur;
  tg0_vtab *p = (tg0_vtab *)cur->pVtab;
  tg0_user_column_kind kind;
  int idx;

  // During an UPDATE, SQLite probes the columns the statement does not SET
  // with the nochange flag: returning without a result here is what makes
  // sqlite3_value_nochange() true for them in tg0Update.
  if (sqlite3_vtab_nochange(context)) {
    return SQLITE_OK;
  }
  // KNN plans serve the emitted row from the values kept at refinement
  // time; every other plan reads the positioned scan statement
  tg0_knn_row *kept =
      pCur->plan == TG0_PLAN_KNN && pCur->knn ? pCur->knn->current : NULL;
#define TG0_SCAN_VALUE(iScan)                                                  \
  (kept ? kept->values[iScan] : sqlite3_column_value(pCur->stmt, iScan))
  if (p->pkColumnName && i == 0) {
    // integer primary keys alias the content rowid (scan column 0); text
    // primary keys resolve through t_rowids (scan column 1)
    sqlite3_result_value(context,
                         TG0_SCAN_VALUE(p->pkColumnType == SQLITE_TEXT ? 1 : 0));
    return SQLITE_OK;
  }
  if (tg0_column_lookup(p, i, &kind, &idx) != SQLITE_OK) {
    // the hidden distance column: the current row's distance from the
    // query point on KNN plans, NULL on every other plan
    if (pCur->plan == TG0_PLAN_KNN && pCur->knn) {
      sqlite3_result_double(context, pCur->knn->currentDistance);
    } else {
      sqlite3_result_null(context);
    }
    return SQLITE_OK;
  }
  int iScan = tg0_scan_header_columns(p) +
              (kind == TG0_USER_COLUMN_KIND_GEOMETRY
                   ? idx
                   : p->numGeometryColumns + idx);
  sqlite3_result_value(context, TG0_SCAN_VALUE(iScan));
#undef TG0_SCAN_VALUE
  return SQLITE_OK;
}

// ---------------------------------------------------------------------------
// tg0 write path (xUpdate): INSERT, DELETE, and UPDATE.
//
// Every write validates first and touches the shadow tables second: geometry
// columns are NOT NULL and strictly typed, data columns are type-checked
// (NULL allowed, integers are accepted into float columns), so those
// constraint failures happen before any shadow row is written. Failures
// after the first shadow write (like a duplicate rowid or text id) are
// rolled back by SQLite's statement-level atomicity: the shadow writes run
// inside the statement transaction that wraps the xUpdate call.
// ---------------------------------------------------------------------------

// The user-facing SQL type keyword for an SQLITE_* fundamental type code.
static const char *tg0_sqlite_type_keyword(int type) {
  switch (type) {
  case SQLITE_INTEGER:
    return "integer";
  case SQLITE_FLOAT:
    return "float";
  case SQLITE_TEXT:
    return "text";
  case SQLITE_BLOB:
    return "blob";
  default:
    return "null";
  }
}

// 1 when value already holds WKB bytes: a blob that isn't sniffed as GeoJSON
// (mirroring geomValue's blob-vs-GeoJSON rules). Those bytes are stored in
// the content table verbatim instead of being re-serialized.
static int tg0_value_is_wkb(sqlite3_value *value) {
  return sqlite3_value_type(value) == SQLITE_BLOB &&
         sqlite3_value_subtype(value) != JSON_SUBTYPE &&
         !(sqlite3_value_bytes(value) > 0 &&
           ((const char *)sqlite3_value_blob(value))[0] == '{');
}

// Parses and validates a value being written to geometry column iGeom: NULL
// is rejected (geometry columns are NOT NULL), the value must parse through
// geomValue(), narrowed columns must match their declared type exactly, and
// the bbox must be finite — and within lon/lat range on geodesic columns,
// so a table holding projected coordinates fails at its first INSERT with a
// message naming `metric=planar` instead of producing garbage distances.
// On success *out_geom is an owned tg_geom the caller must tg_geom_free().
// On failure the vtab error is set and SQLITE_CONSTRAINT (NULL value / type
// mismatch / bad coordinates) or SQLITE_ERROR (unparseable input) is
// returned.
static int tg0_validate_geometry_value(tg0_vtab *p, int iGeom,
                                       sqlite3_value *value,
                                       struct tg_geom **out_geom) {
  struct tg0_geometry_column *column = &p->geometry_columns[iGeom];
  struct tg_geom *geom = NULL;
  char *errmsg = NULL;

  if (sqlite3_value_type(value) == SQLITE_NULL &&
      sqlite3_value_pointer(value, TG_GEOM_POINTER_NAME) == NULL) {
    tg_vtab_set_error(&p->base, "%s.%s: geometry columns cannot be NULL",
                      p->tableName, column->name);
    return SQLITE_CONSTRAINT;
  }
  if (geomValue(value, &geom, &errmsg) != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "%s.%s: %s", p->tableName, column->name,
                      errmsg);
    sqlite3_free(errmsg);
    return SQLITE_ERROR;
  }
  if (column->geomType != 0 && (int)tg_geom_typeof(geom) != column->geomType) {
    tg_vtab_set_error(&p->base, "%s.%s: expected a %s, received a %s",
                      p->tableName, column->name,
                      tg_geom_type_string(column->geomType),
                      tg_geom_type_string(tg_geom_typeof(geom)));
    tg_geom_free(geom);
    return SQLITE_CONSTRAINT;
  }
  struct tg_rect rect = tg_geom_rect(geom);
  if (column->metric == TG_METRIC_GEODESIC
          ? !(tg_lonlat_valid(rect.min.x, rect.min.y) &&
              tg_lonlat_valid(rect.max.x, rect.max.y))
          : !(isfinite(rect.min.x) && isfinite(rect.min.y) &&
              isfinite(rect.max.x) && isfinite(rect.max.y))) {
    if (column->metric == TG_METRIC_GEODESIC) {
      tg_vtab_set_error(&p->base,
                        "%s.%s: coordinates outside lon/lat range (x=%g, "
                        "y=%g); declare the column with metric=planar for "
                        "projected coordinates",
                        p->tableName, column->name,
                        fabs(rect.min.x) > fabs(rect.max.x) || isnan(rect.min.x)
                            ? rect.min.x
                            : rect.max.x,
                        fabs(rect.min.y) > fabs(rect.max.y) || isnan(rect.min.y)
                            ? rect.min.y
                            : rect.max.y);
    } else {
      tg_vtab_set_error(&p->base, "%s.%s: coordinates must be finite",
                        p->tableName, column->name);
    }
    tg_geom_free(geom);
    return SQLITE_CONSTRAINT;
  }
  *out_geom = geom;
  return SQLITE_OK;
}

// Validates a value being written to data column iData against its declared
// type. NULL is always allowed, and integers are accepted into float columns
// (the affinity behavior SQLite users expect from an ordinary REAL column);
// everything else must match exactly.
static int tg0_validate_data_value(tg0_vtab *p, int iData,
                                   sqlite3_value *value) {
  struct tg0_data_column *column = &p->data_columns[iData];
  int type = sqlite3_value_type(value);
  if (type == SQLITE_NULL || type == column->type ||
      (column->type == SQLITE_FLOAT && type == SQLITE_INTEGER)) {
    return SQLITE_OK;
  }
  tg_vtab_set_error(&p->base, "%s.%s: expected %s value, received %s",
                    p->tableName, column->name,
                    tg0_sqlite_type_keyword(column->type),
                    tg0_sqlite_type_keyword(type));
  return SQLITE_CONSTRAINT;
}

// Binds a value being written to data column iData. Integers going into
// float columns are bound as doubles so the stored type matches the declared
// column type; everything else binds verbatim.
static int tg0_bind_data_value(sqlite3_stmt *stmt, int iParam,
                               struct tg0_data_column *column,
                               sqlite3_value *value) {
  if (column->type == SQLITE_FLOAT &&
      sqlite3_value_type(value) == SQLITE_INTEGER) {
    return sqlite3_bind_double(stmt, iParam, sqlite3_value_double(value));
  }
  return sqlite3_bind_value(stmt, iParam, value);
}

// Binds a geometry column's content blob: the input value's bytes verbatim
// when the input already is WKB, a fresh tg_geom_wkb serialization of geom
// otherwise.
static int tg0_bind_geometry_wkb(sqlite3_stmt *stmt, int iParam,
                                 sqlite3_value *value, struct tg_geom *geom) {
  if (tg0_value_is_wkb(value)) {
    return sqlite3_bind_value(stmt, iParam, value);
  }
  size_t size = tg_geom_wkb(geom, NULL, 0);
  uint8_t *buffer = sqlite3_malloc64(size + 1);
  if (!buffer) {
    return SQLITE_NOMEM;
  }
  tg_geom_wkb(geom, buffer, size + 1);
  return sqlite3_bind_blob64(stmt, iParam, buffer, size, sqlite3_free);
}

// Prepares an sqlite3_mprintf'd statement against a shadow table, freeing
// zSql. A NULL zSql (OOM upstream) returns SQLITE_NOMEM; prepare failures
// set the vtab error (including sqlite3_errmsg) and return SQLITE_ERROR.
static int tg0_write_prepare(tg0_vtab *p, char *zSql, sqlite3_stmt **out_stmt,
                             const char *zWhat) {
  int rc;
  *out_stmt = NULL;
  if (!zSql) {
    return SQLITE_NOMEM;
  }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, out_stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "%s: %s", zWhat, sqlite3_errmsg(p->db));
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

// Steps a fully-bound shadow-table DML statement to completion and finalizes
// it. Failures set the vtab error (including sqlite3_errmsg) and return the
// step's result code, so constraint violations (like a duplicate rowid or
// text id) propagate as SQLITE_CONSTRAINT.
static int tg0_write_step(tg0_vtab *p, sqlite3_stmt *stmt, const char *zWhat) {
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return SQLITE_OK;
  }
  tg_vtab_set_error(&p->base, "%s: %s", zWhat, sqlite3_errmsg(p->db));
  sqlite3_finalize(stmt);
  return rc == SQLITE_ROW ? SQLITE_ERROR : rc;
}

// Returns the cached statement in *slot, preparing zSql (sqlite3_mprintf'd,
// freed here) on first use. Same error contract as tg0_write_prepare.
static int tg0_cached_prepare(tg0_vtab *p, sqlite3_stmt **slot, char *zSql,
                              const char *zWhat) {
  if (*slot) {
    sqlite3_free(zSql);
    return SQLITE_OK;
  }
  return tg0_write_prepare(p, zSql, slot, zWhat);
}

// Resets a cached statement and clears its bindings so the next use (or a
// bind failure's error path) never sees stale parameter values.
static void tg0_cached_reset(sqlite3_stmt *stmt) {
  if (stmt) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }
}

// Steps a fully-bound cached DML statement to completion and resets it.
// Same error contract as tg0_write_step, minus the finalize.
static int tg0_cached_step(tg0_vtab *p, sqlite3_stmt *stmt, const char *zWhat) {
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    tg_vtab_set_error(&p->base, "%s: %s", zWhat, sqlite3_errmsg(p->db));
    rc = rc == SQLITE_ROW ? SQLITE_ERROR : rc;
  } else {
    rc = SQLITE_OK;
  }
  tg0_cached_reset(stmt);
  return rc;
}

// Resolves the argv[0] rowid/primary-key value of a DELETE or UPDATE to the
// int64 rowid the shadow tables are keyed by. Text primary keys go through
// the t_rowids mapping table; everything else is the value itself.
static int tg0_resolve_rowid(tg0_vtab *p, sqlite3_value *idValue,
                             sqlite3_int64 *out_rowid) {
  if (p->pkColumnType != SQLITE_TEXT) {
    *out_rowid = sqlite3_value_int64(idValue);
    return SQLITE_OK;
  }
  int rc = tg0_cached_prepare(
      p, &p->stmtRowidsLookup,
      sqlite3_mprintf("SELECT rowid FROM " TG0_SHADOW_ROWIDS_NAME
                      " WHERE id = ?1",
                      p->schemaName, p->tableName),
      "Error preparing tg0 rowids lookup");
  if (rc != SQLITE_OK) {
    return rc;
  }
  sqlite3_stmt *stmt = p->stmtRowidsLookup;
  rc = sqlite3_bind_value(stmt, 1, idValue);
  if (rc != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "Error binding tg0 rowids lookup: %s",
                      sqlite3_errmsg(p->db));
    tg0_cached_reset(stmt);
    return rc;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *out_rowid = sqlite3_column_int64(stmt, 0);
    tg0_cached_reset(stmt);
    return SQLITE_OK;
  }
  if (rc == SQLITE_DONE) {
    // clean scan, no mapping row: the primary key does not exist
    tg_vtab_set_error(&p->base, "Could not find a tg0 row with primary key %Q",
                      (const char *)sqlite3_value_text(idValue));
    tg0_cached_reset(stmt);
    return SQLITE_ERROR;
  }
  tg_vtab_set_error(&p->base, "Error looking up tg0 primary key: %s",
                    sqlite3_errmsg(p->db));
  tg0_cached_reset(stmt);
  return rc;
}

// The hidden distance column (reserved for KNN) is read-only: only NULL or
// an unchanged value may be written to it.
static int tg0_check_distance_write(tg0_vtab *p, sqlite3_value *value) {
  if (sqlite3_value_nochange(value) ||
      sqlite3_value_type(value) == SQLITE_NULL) {
    return SQLITE_OK;
  }
  tg_vtab_set_error(&p->base,
                    "%s.distance: the distance column is reserved and cannot "
                    "be written to",
                    p->tableName);
  return SQLITE_ERROR;
}

// 1 when a and b hold different values under sqlite3_value semantics — used
// to detect rowid/primary-key changes (argv[0] vs argv[1] of an UPDATE).
static int tg0_value_changed(sqlite3_value *a, sqlite3_value *b) {
  int typeA = sqlite3_value_type(a);
  int typeB = sqlite3_value_type(b);
  if (typeA != typeB) {
    return 1;
  }
  if (typeA == SQLITE_INTEGER) {
    return sqlite3_value_int64(a) != sqlite3_value_int64(b);
  }
  if (typeA == SQLITE_FLOAT) {
    return sqlite3_value_double(a) != sqlite3_value_double(b);
  }
  if (typeA == SQLITE_TEXT || typeA == SQLITE_BLOB) {
    const void *dataA = typeA == SQLITE_TEXT
                            ? (const void *)sqlite3_value_text(a)
                            : sqlite3_value_blob(a);
    const void *dataB = typeB == SQLITE_TEXT
                            ? (const void *)sqlite3_value_text(b)
                            : sqlite3_value_blob(b);
    int nA = sqlite3_value_bytes(a);
    int nB = sqlite3_value_bytes(b);
    return nA != nB || memcmp(dataA, dataB, nA) != 0;
  }
  return 0;
}

// INSERT: validate every column value, resolve the new rowid (t_rowids
// assigns one for text primary keys; otherwise argv[1] or content-table
// autoassign), then write content + one rtree row per geometry column.
static int tg0_write_insert(tg0_vtab *p, int argc, sqlite3_value **argv,
                            sqlite_int64 *pRowid) {
  struct tg_geom *geoms[TG0_MAX_GEOMETRY_COLUMNS] = {NULL};
  sqlite3_stmt *stmt = NULL;
  sqlite3_int64 rowid = 0;
  int hasRowid = 0;
  int pkOffset = tg0_pk_column_offset(p);
  int rc;

  rc = tg0_check_distance_write(p, argv[2 + tg0_column_distance_idx(p)]);
  if (rc != SQLITE_OK) {
    return rc;
  }

  // validate every geometry and data value before the first shadow write,
  // so these constraint failures leave no partial rows behind
  for (int i = 0; i < tg0_num_user_columns(p); i++) {
    sqlite3_value *value = argv[2 + pkOffset + i];
    int idx = p->user_column_idxs[i];
    if (p->user_column_kinds[i] == TG0_USER_COLUMN_KIND_GEOMETRY) {
      rc = tg0_validate_geometry_value(p, idx, value, &geoms[idx]);
    } else {
      rc = tg0_validate_data_value(p, idx, value);
    }
    if (rc != SQLITE_OK) {
      goto done;
    }
  }

  // resolve the new row's rowid. SQLite does not treat a declared primary
  // key as a rowid alias for virtual tables: the pk value arrives in the pk
  // column's own slot (argv[2] — the pk is always declared column 0), and
  // argv[1] only carries an explicit `rowid` on rowid tables.
  if (p->pkColumnType == SQLITE_TEXT) {
    // the declared primary key is the text id; t_rowids assigns the int64
    // rowid that the content table and rtrees are keyed by
    sqlite3_value *idValue = argv[2];
    if (sqlite3_value_type(idValue) != SQLITE_TEXT) {
      if (sqlite3_value_type(idValue) == SQLITE_NULL) {
        tg_vtab_set_error(&p->base, "%s.%s: primary key cannot be NULL",
                          p->tableName, p->pkColumnName);
      } else {
        tg_vtab_set_error(
            &p->base, "%s.%s: expected text primary key, received %s",
            p->tableName, p->pkColumnName,
            tg0_sqlite_type_keyword(sqlite3_value_type(idValue)));
      }
      rc = SQLITE_CONSTRAINT;
      goto done;
    }
    rc = tg0_cached_prepare(p, &p->stmtRowidsInsert,
                            sqlite3_mprintf("INSERT INTO " TG0_SHADOW_ROWIDS_NAME
                                            "(id) VALUES (?1)",
                                            p->schemaName, p->tableName),
                            "Error preparing tg0 rowids insert");
    if (rc != SQLITE_OK) {
      goto done;
    }
    stmt = p->stmtRowidsInsert;
    rc = sqlite3_bind_value(stmt, 1, idValue);
    if (rc != SQLITE_OK) {
      tg_vtab_set_error(&p->base, "Error binding tg0 rowids insert: %s",
                        sqlite3_errmsg(p->db));
      goto done;
    }
    // a duplicate id surfaces here as the id column's UNIQUE violation
    rc = tg0_cached_step(p, stmt, "Error inserting tg0 primary key");
    stmt = NULL;
    if (rc != SQLITE_OK) {
      goto done;
    }
    rowid = sqlite3_last_insert_rowid(p->db);
    hasRowid = 1;
  } else {
    sqlite3_value *rowidValue = argv[1];
    if (p->pkColumnType == SQLITE_INTEGER &&
        sqlite3_value_type(argv[2]) != SQLITE_NULL) {
      rowidValue = argv[2];
    }
    if (sqlite3_value_type(rowidValue) == SQLITE_INTEGER) {
      rowid = sqlite3_value_int64(rowidValue);
      hasRowid = 1;
    } else if (sqlite3_value_type(rowidValue) != SQLITE_NULL) {
      tg_vtab_set_error(&p->base, "%s: rowid values must be integers",
                        p->tableName);
      rc = SQLITE_MISMATCH;
      goto done;
    }
  }

  // content row: rowid (NULL autoassigns) + geometry WKB + data values
  if (!p->stmtContentInsert) {
    sqlite3_str *str = sqlite3_str_new(NULL);
    sqlite3_str_appendf(str, "INSERT INTO " TG0_SHADOW_CONTENT_NAME "(rowid",
                        p->schemaName, p->tableName);
    for (int i = 0; i < p->numGeometryColumns; i++) {
      sqlite3_str_appendf(str, ", geom%02d", i);
    }
    for (int i = 0; i < p->numDataColumns; i++) {
      sqlite3_str_appendf(str, ", data%02d", i);
    }
    sqlite3_str_appendall(str, ") VALUES (?");
    for (int i = 0; i < tg0_num_user_columns(p); i++) {
      sqlite3_str_appendall(str, ", ?");
    }
    sqlite3_str_appendall(str, ")");
    rc = tg0_cached_prepare(p, &p->stmtContentInsert, sqlite3_str_finish(str),
                            "Error preparing tg0 content insert");
    if (rc != SQLITE_OK) {
      goto done;
    }
  }
  stmt = p->stmtContentInsert;
  rc = hasRowid ? sqlite3_bind_int64(stmt, 1, rowid)
                : sqlite3_bind_null(stmt, 1);
  for (int i = 0; i < tg0_num_user_columns(p) && rc == SQLITE_OK; i++) {
    sqlite3_value *value = argv[2 + pkOffset + i];
    int idx = p->user_column_idxs[i];
    if (p->user_column_kinds[i] == TG0_USER_COLUMN_KIND_GEOMETRY) {
      rc = tg0_bind_geometry_wkb(stmt, 2 + idx, value, geoms[idx]);
    } else {
      rc = tg0_bind_data_value(stmt, 2 + p->numGeometryColumns + idx,
                               &p->data_columns[idx], value);
    }
  }
  if (rc != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "Error binding tg0 content insert: %s",
                      sqlite3_errmsg(p->db));
    goto done;
  }
  rc = tg0_cached_step(p, stmt, "Error inserting tg0 content row");
  stmt = NULL;
  if (rc != SQLITE_OK) {
    goto done;
  }
  if (!hasRowid) {
    rowid = sqlite3_last_insert_rowid(p->db);
  }

  // one bbox row per geometry column's rtree
  for (int i = 0; i < p->numGeometryColumns; i++) {
    struct tg_rect rect = tg_geom_rect(geoms[i]);
    rc = tg0_cached_prepare(
        p, &p->stmtRtreeInsert[i],
        sqlite3_mprintf("INSERT INTO " TG0_SHADOW_RTREE_NAME
                        "(id, minX, maxX, minY, maxY)"
                        " VALUES (?1, ?2, ?3, ?4, ?5)",
                        p->schemaName, p->tableName, i),
        "Error preparing tg0 rtree insert");
    if (rc != SQLITE_OK) {
      goto done;
    }
    stmt = p->stmtRtreeInsert[i];
    sqlite3_bind_int64(stmt, 1, rowid);
    sqlite3_bind_double(stmt, 2, rect.min.x);
    sqlite3_bind_double(stmt, 3, rect.max.x);
    sqlite3_bind_double(stmt, 4, rect.min.y);
    sqlite3_bind_double(stmt, 5, rect.max.y);
    rc = tg0_cached_step(p, stmt, "Error inserting tg0 rtree row");
    stmt = NULL;
    if (rc != SQLITE_OK) {
      goto done;
    }
  }
  *pRowid = rowid;

done:
  // stmt is non-NULL only when a cached statement was left bound by an
  // error path before it was stepped
  tg0_cached_reset(stmt);
  for (int i = 0; i < p->numGeometryColumns; i++) {
    tg_geom_free(geoms[i]);
  }
  return rc;
}

// DELETE: remove the content row, every rtree entry, and the rowids mapping
// row on text-primary-key tables. Every step failure propagates.
static int tg0_write_delete(tg0_vtab *p, sqlite3_value *idValue) {
  sqlite3_stmt *stmt;
  sqlite3_int64 rowid;
  int rc = tg0_resolve_rowid(p, idValue, &rowid);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = tg0_cached_prepare(p, &p->stmtContentDelete,
                          sqlite3_mprintf("DELETE FROM " TG0_SHADOW_CONTENT_NAME
                                          " WHERE rowid = ?1",
                                          p->schemaName, p->tableName),
                          "Error preparing tg0 content delete");
  if (rc != SQLITE_OK) {
    return rc;
  }
  stmt = p->stmtContentDelete;
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = tg0_cached_step(p, stmt, "Error deleting tg0 content row");
  if (rc != SQLITE_OK) {
    return rc;
  }
  for (int i = 0; i < p->numGeometryColumns; i++) {
    rc = tg0_cached_prepare(p, &p->stmtRtreeDelete[i],
                            sqlite3_mprintf("DELETE FROM " TG0_SHADOW_RTREE_NAME
                                            " WHERE id = ?1",
                                            p->schemaName, p->tableName, i),
                            "Error preparing tg0 rtree delete");
    if (rc != SQLITE_OK) {
      return rc;
    }
    stmt = p->stmtRtreeDelete[i];
    sqlite3_bind_int64(stmt, 1, rowid);
    rc = tg0_cached_step(p, stmt, "Error deleting tg0 rtree row");
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  if (p->pkColumnType == SQLITE_TEXT) {
    rc = tg0_cached_prepare(p, &p->stmtRowidsDelete,
                            sqlite3_mprintf("DELETE FROM " TG0_SHADOW_ROWIDS_NAME
                                            " WHERE rowid = ?1",
                                            p->schemaName, p->tableName),
                            "Error preparing tg0 rowids delete");
    if (rc != SQLITE_OK) {
      return rc;
    }
    stmt = p->stmtRowidsDelete;
    sqlite3_bind_int64(stmt, 1, rowid);
    rc = tg0_cached_step(p, stmt, "Error deleting tg0 rowids row");
  }
  return rc;
}

// UPDATE: sqlite3_value_nochange()-aware — unchanged columns are skipped
// entirely, changed geometry columns get INSERT-grade validation plus an
// rtree bbox update, and data-only updates leave every rtree untouched.
// Changing the rowid/primary key itself is not supported.
static int tg0_write_update(tg0_vtab *p, int argc, sqlite3_value **argv) {
  struct tg_geom *geoms[TG0_MAX_GEOMETRY_COLUMNS] = {NULL};
  sqlite3_value *geomInputs[TG0_MAX_GEOMETRY_COLUMNS] = {NULL};
  sqlite3_value *dataValues[TG0_MAX_DATA_COLUMNS] = {NULL};
  sqlite3_stmt *stmt = NULL;
  sqlite3_int64 rowid;
  int pkOffset = tg0_pk_column_offset(p);
  int nChanged = 0;
  int iParam;
  int rc;

  // argv[0] vs argv[1] catches rowid changes and text primary key changes
  // (argv[1] is the new pk on WITHOUT ROWID tables). Integer primary keys
  // are not rowid-aliased by SQLite for virtual tables, so a `SET id = ...`
  // arrives in the pk column's own slot (argv[2]) instead.
  if ((!sqlite3_value_nochange(argv[1]) &&
       tg0_value_changed(argv[0], argv[1])) ||
      (p->pkColumnType == SQLITE_INTEGER &&
       !sqlite3_value_nochange(argv[2]) &&
       tg0_value_changed(argv[0], argv[2]))) {
    tg_vtab_set_error(&p->base,
                      "changing the primary key of a tg0 row is not supported");
    return SQLITE_ERROR;
  }
  rc = tg0_check_distance_write(p, argv[2 + tg0_column_distance_idx(p)]);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = tg0_resolve_rowid(p, argv[0], &rowid);
  if (rc != SQLITE_OK) {
    return rc;
  }

  // validate every changed value before the first shadow write. A changed
  // geometry column is marked by its non-NULL geoms[] entry, a changed data
  // column by its non-NULL dataValues[] entry (NULL SQL values still change
  // a data column, so track the sqlite3_value itself).
  int dataChanged[TG0_MAX_DATA_COLUMNS] = {0};
  for (int i = 0; i < tg0_num_user_columns(p); i++) {
    sqlite3_value *value = argv[2 + pkOffset + i];
    int idx = p->user_column_idxs[i];
    if (sqlite3_value_nochange(value)) {
      continue;
    }
    if (p->user_column_kinds[i] == TG0_USER_COLUMN_KIND_GEOMETRY) {
      rc = tg0_validate_geometry_value(p, idx, value, &geoms[idx]);
      geomInputs[idx] = value;
    } else {
      rc = tg0_validate_data_value(p, idx, value);
      dataValues[idx] = value;
      dataChanged[idx] = 1;
    }
    if (rc != SQLITE_OK) {
      goto done;
    }
    nChanged++;
  }
  if (nChanged == 0) {
    rc = SQLITE_OK;
    goto done;
  }

  // one UPDATE for all changed content columns
  sqlite3_str *str = sqlite3_str_new(NULL);
  sqlite3_str_appendf(str, "UPDATE " TG0_SHADOW_CONTENT_NAME " SET ",
                      p->schemaName, p->tableName);
  iParam = 1;
  for (int i = 0; i < p->numGeometryColumns; i++) {
    if (geoms[i]) {
      sqlite3_str_appendf(str, "%sgeom%02d = ?%d", iParam > 1 ? ", " : "", i,
                          iParam);
      iParam++;
    }
  }
  for (int i = 0; i < p->numDataColumns; i++) {
    if (dataChanged[i]) {
      sqlite3_str_appendf(str, "%sdata%02d = ?%d", iParam > 1 ? ", " : "", i,
                          iParam);
      iParam++;
    }
  }
  sqlite3_str_appendf(str, " WHERE rowid = ?%d", iParam);
  rc = tg0_write_prepare(p, sqlite3_str_finish(str), &stmt,
                         "Error preparing tg0 content update");
  if (rc != SQLITE_OK) {
    goto done;
  }
  iParam = 1;
  rc = SQLITE_OK;
  for (int i = 0; i < p->numGeometryColumns && rc == SQLITE_OK; i++) {
    if (geoms[i]) {
      rc = tg0_bind_geometry_wkb(stmt, iParam++, geomInputs[i], geoms[i]);
    }
  }
  for (int i = 0; i < p->numDataColumns && rc == SQLITE_OK; i++) {
    if (dataChanged[i]) {
      rc = tg0_bind_data_value(stmt, iParam++, &p->data_columns[i],
                               dataValues[i]);
    }
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, iParam, rowid);
  }
  if (rc != SQLITE_OK) {
    tg_vtab_set_error(&p->base, "Error binding tg0 content update: %s",
                      sqlite3_errmsg(p->db));
    goto done;
  }
  rc = tg0_write_step(p, stmt, "Error updating tg0 content row");
  stmt = NULL;
  if (rc != SQLITE_OK) {
    goto done;
  }

  // move each changed geometry column's rtree bbox
  for (int i = 0; i < p->numGeometryColumns; i++) {
    if (!geoms[i]) {
      continue;
    }
    struct tg_rect rect = tg_geom_rect(geoms[i]);
    rc = tg0_cached_prepare(
        p, &p->stmtRtreeUpdate[i],
        sqlite3_mprintf("UPDATE " TG0_SHADOW_RTREE_NAME
                        " SET minX = ?2, maxX = ?3, minY = ?4, maxY = ?5"
                        " WHERE id = ?1",
                        p->schemaName, p->tableName, i),
        "Error preparing tg0 rtree update");
    if (rc != SQLITE_OK) {
      goto done;
    }
    sqlite3_stmt *cached = p->stmtRtreeUpdate[i];
    sqlite3_bind_int64(cached, 1, rowid);
    sqlite3_bind_double(cached, 2, rect.min.x);
    sqlite3_bind_double(cached, 3, rect.max.x);
    sqlite3_bind_double(cached, 4, rect.min.y);
    sqlite3_bind_double(cached, 5, rect.max.y);
    rc = tg0_cached_step(p, cached, "Error updating tg0 rtree row");
    if (rc != SQLITE_OK) {
      goto done;
    }
  }

done:
  // the content UPDATE is built per call (its SET list depends on which
  // columns changed) and is the only finalized statement left here
  sqlite3_finalize(stmt);
  for (int i = 0; i < p->numGeometryColumns; i++) {
    tg_geom_free(geoms[i]);
  }
  return rc;
}

static int tg0Update(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                     sqlite_int64 *pRowid) {
  tg0_vtab *p = (tg0_vtab *)pVTab;
  if (argc == 1) {
    return tg0_write_delete(p, argv[0]);
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    return tg0_write_insert(p, argc, argv, pRowid);
  }
  return tg0_write_update(p, argc, argv);
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
  if (sqlite3_stricmp(zName, "tg_touches") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_touches;
    return TG0_FUNC_TOUCHES;
  }
  if (sqlite3_stricmp(zName, "tg_equals") == 0 && nArg == 2) {
    *pxFunc = tg_predicate_impl;
    *ppArg = tg_geom_equals;
    return TG0_FUNC_EQUALS;
  }
  return 0;
};

// xRename: `ALTER TABLE ... RENAME TO zNew` renames every shadow table to
// the new name's prefix — info, content, one rtree per geometry column, and
// rowids for text-primary-key tables. Renaming each t_rtreeNN (an rtree
// virtual table) triggers the rtree module's own xRename, which moves its
// _node/_parent/_rowid shadow tables along with it. On failure the error is
// reported on the vtab and SQLite rolls back the containing ALTER TABLE
// statement, un-renaming whatever shadow tables already moved.
static int tg0Rename(sqlite3_vtab *pVtab, const char *zNew) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  char *zErrMsg = NULL;
  int rc;

  // cached write statements embed the old shadow table names
  tg0_free_statements(p);

  char *zNewName = sqlite3_mprintf("%s", zNew);
  if (!zNewName) {
    return SQLITE_NOMEM;
  }

  rc = tg0_shadow_sql(p->db,
                      sqlite3_mprintf("ALTER TABLE " TG0_SHADOW_INFO_NAME
                                      " RENAME TO \"%w_info\"",
                                      p->schemaName, p->tableName, zNew),
                      "Could not rename the tg0 info shadow table", &zErrMsg);
  if (rc == SQLITE_OK) {
    rc = tg0_shadow_sql(p->db,
                        sqlite3_mprintf("ALTER TABLE " TG0_SHADOW_CONTENT_NAME
                                        " RENAME TO \"%w_content\"",
                                        p->schemaName, p->tableName, zNew),
                        "Could not rename the tg0 content shadow table",
                        &zErrMsg);
  }
  for (int i = 0; i < p->numGeometryColumns && rc == SQLITE_OK; i++) {
    rc = tg0_shadow_sql(
        p->db,
        sqlite3_mprintf("ALTER TABLE " TG0_SHADOW_RTREE_NAME
                        " RENAME TO \"%w_rtree%02d\"",
                        p->schemaName, p->tableName, i, zNew, i),
        "Could not rename a tg0 rtree shadow table", &zErrMsg);
  }
  if (rc == SQLITE_OK && p->pkColumnType == SQLITE_TEXT) {
    rc = tg0_shadow_sql(p->db,
                        sqlite3_mprintf("ALTER TABLE " TG0_SHADOW_ROWIDS_NAME
                                        " RENAME TO \"%w_rowids\"",
                                        p->schemaName, p->tableName, zNew),
                        "Could not rename the tg0 rowids shadow table",
                        &zErrMsg);
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(zNewName);
    sqlite3_free(pVtab->zErrMsg);
    pVtab->zErrMsg = zErrMsg;
    return rc;
  }
  // xRename receives the new table name; the vtab keeps serving this
  // connection after the ALTER, so its shadow SQL must use the new prefix.
  // The schema name never changes on a rename.
  sqlite3_free(p->tableName);
  p->tableName = zNewName;
  return SQLITE_OK;
}

// Runs one integrity probe: zSql is an sqlite3_mprintf'd single-row
// `SELECT count(*) ...`, freed here. On success *outCount is the count. A
// NULL zSql or an OOM from SQLite is an internal malfunction (SQLITE_NOMEM);
// any other prepare/step failure — a missing or malformed shadow table —
// is itself an integrity problem, reported through *pzProblem with
// SQLITE_OK, matching xIntegrity's only-malfunctions-error contract.
static int tg0_integrity_probe(tg0_vtab *p, char *zSql, sqlite3_int64 *outCount,
                               char **pzProblem) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  *outCount = 0;
  if (!zSql) {
    return SQLITE_NOMEM;
  }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *outCount = sqlite3_column_int64(stmt, 0);
    rc = SQLITE_OK;
  } else if (rc == SQLITE_NOMEM) {
    // fall through: internal malfunction
  } else {
    *pzProblem =
        sqlite3_mprintf("tg0 table %s: integrity check query failed: %s",
                        p->tableName, sqlite3_errmsg(p->db));
    rc = *pzProblem ? SQLITE_OK : SQLITE_NOMEM;
  }
  sqlite3_finalize(stmt);
  sqlite3_free(zSql);
  return rc;
}

// xIntegrity (iVersion 4): PRAGMA integrity_check / quick_check hook.
// Verifies the content↔rtree 1:1 invariant (decision 11: geometry columns
// are NOT NULL, so every content row has exactly one entry in every rtree)
// and, for text primary keys, the content↔rowids 1:1 mapping. The first
// problem found is reported through *pzErr with SQLITE_OK — integrity_check
// prints the message; only internal malfunctions return non-OK.
static int tg0Integrity(sqlite3_vtab *pVtab, const char *zSchema,
                        const char *zTabName, int mFlags, char **pzErr) {
  tg0_vtab *p = (tg0_vtab *)pVtab;
  sqlite3_int64 count;
  int rc;
  (void)zSchema;
  (void)zTabName;
  (void)mFlags;
  *pzErr = NULL;

  for (int i = 0; i < p->numGeometryColumns; i++) {
    rc = tg0_integrity_probe(
        p,
        sqlite3_mprintf("SELECT count(*) FROM " TG0_SHADOW_CONTENT_NAME
                        " AS c WHERE NOT EXISTS (SELECT 1 FROM "
                        TG0_SHADOW_RTREE_NAME " AS i WHERE i.id = c.rowid)",
                        p->schemaName, p->tableName, p->schemaName,
                        p->tableName, i),
        &count, pzErr);
    if (rc != SQLITE_OK || *pzErr) {
      return rc;
    }
    if (count > 0) {
      *pzErr = sqlite3_mprintf(
          "tg0 table %s: %lld content row(s) have no %s_rtree%02d entry "
          "(geometry column \"%w\")",
          p->tableName, count, p->tableName, i, p->geometry_columns[i].name);
      return *pzErr ? SQLITE_OK : SQLITE_NOMEM;
    }
    rc = tg0_integrity_probe(
        p,
        sqlite3_mprintf("SELECT count(*) FROM " TG0_SHADOW_RTREE_NAME
                        " AS i WHERE NOT EXISTS (SELECT 1 FROM "
                        TG0_SHADOW_CONTENT_NAME " AS c WHERE c.rowid = i.id)",
                        p->schemaName, p->tableName, i, p->schemaName,
                        p->tableName),
        &count, pzErr);
    if (rc != SQLITE_OK || *pzErr) {
      return rc;
    }
    if (count > 0) {
      *pzErr = sqlite3_mprintf(
          "tg0 table %s: %lld orphaned %s_rtree%02d entr%s with no content "
          "row (geometry column \"%w\")",
          p->tableName, count, p->tableName, i, count == 1 ? "y" : "ies",
          p->geometry_columns[i].name);
      return *pzErr ? SQLITE_OK : SQLITE_NOMEM;
    }
  }

  if (p->pkColumnType == SQLITE_TEXT) {
    rc = tg0_integrity_probe(
        p,
        sqlite3_mprintf("SELECT count(*) FROM " TG0_SHADOW_CONTENT_NAME
                        " AS c WHERE NOT EXISTS (SELECT 1 FROM "
                        TG0_SHADOW_ROWIDS_NAME " AS r WHERE r.rowid = c.rowid)",
                        p->schemaName, p->tableName, p->schemaName,
                        p->tableName),
        &count, pzErr);
    if (rc != SQLITE_OK || *pzErr) {
      return rc;
    }
    if (count > 0) {
      *pzErr = sqlite3_mprintf("tg0 table %s: %lld content row(s) have no "
                               "%s_rowids primary key mapping",
                               p->tableName, count, p->tableName);
      return *pzErr ? SQLITE_OK : SQLITE_NOMEM;
    }
    rc = tg0_integrity_probe(
        p,
        sqlite3_mprintf("SELECT count(*) FROM " TG0_SHADOW_ROWIDS_NAME
                        " AS r WHERE NOT EXISTS (SELECT 1 FROM "
                        TG0_SHADOW_CONTENT_NAME " AS c WHERE c.rowid = r.rowid)",
                        p->schemaName, p->tableName, p->schemaName,
                        p->tableName),
        &count, pzErr);
    if (rc != SQLITE_OK || *pzErr) {
      return rc;
    }
    if (count > 0) {
      *pzErr = sqlite3_mprintf("tg0 table %s: %lld orphaned %s_rowids "
                               "mapping row(s) with no content row",
                               p->tableName, count, p->tableName);
      return *pzErr ? SQLITE_OK : SQLITE_NOMEM;
    }
  }

  return SQLITE_OK;
}

// Claims the tg0 shadow tables (info, content, rowids, rtreeNN) plus the
// rtree module's own shadow tables underneath each bbox rtree (rtreeNN_node,
// rtreeNN_parent, rtreeNN_rowid). zName is the suffix after the virtual
// table's name; rtree names are matched by prefix + digits rather than a
// fixed list so every possible NN is covered.
static int tg0ShadowName(const char *zName) {
  if (sqlite3_stricmp(zName, "info") == 0 ||
      sqlite3_stricmp(zName, "content") == 0 ||
      sqlite3_stricmp(zName, "rowids") == 0) {
    return 1;
  }
  if (sqlite3_strnicmp(zName, "rtree", 5) != 0 || !tg0_is_digit(zName[5]) ||
      !tg0_is_digit(zName[6])) {
    return 0;
  }
  zName += 7;
  return *zName == '\0' || sqlite3_stricmp(zName, "_node") == 0 ||
         sqlite3_stricmp(zName, "_parent") == 0 ||
         sqlite3_stricmp(zName, "_rowid") == 0;
}

// The sqlite3_module fields shared by both tg0Module declarations below.
// iVersion 4 declares the xIntegrity method (PRAGMA integrity_check
// support, SQLite 3.44+); older hosts never read past xShadowName.
#define TG0_MODULE_BASE_FIELDS                                                 \
    /* iVersion      */ 4,                                                     \
    /* xCreate       */ tg0Create,                                             \
    /* xConnect      */ tg0Connect,                                            \
    /* xBestIndex    */ tg0BestIndex,                                          \
    /* xDisconnect   */ tg0Disconnect,                                         \
    /* xDestroy      */ tg0Destroy,                                            \
    /* xOpen         */ tg0Open,                                               \
    /* xClose        */ tg0Close,                                              \
    /* xFilter       */ tg0Filter,                                             \
    /* xNext         */ tg0Next,                                               \
    /* xEof          */ tg0Eof,                                                \
    /* xColumn       */ tg0Column,                                             \
    /* xRowid        */ tg0Rowid,                                              \
    /* xUpdate       */ tg0Update,                                             \
    /* xBegin        */ 0,                                                     \
    /* xSync         */ 0,                                                     \
    /* xCommit       */ 0,                                                     \
    /* xRollback     */ 0,                                                     \
    /* xFindFunction */ tg0FindFunction,                                       \
    /* xRename       */ tg0Rename,                                             \
    /* xSavepoint    */ 0,                                                     \
    /* xRelease      */ 0,                                                     \
    /* xRollbackTo   */ 0,                                                     \
    /* xShadowName   */ tg0ShadowName

#if SQLITE_VERSION_NUMBER >= 3044000
static sqlite3_module tg0Module = {TG0_MODULE_BASE_FIELDS,
                                   /* xIntegrity    */ tg0Integrity};
#else
// this sqlite3.h predates the xIntegrity field (added in SQLite 3.44), so
// lay the iVersion-4 module struct out manually: the layouts are identical
// (xIntegrity sits directly after xShadowName), a 3.44+ host loading this
// build reads xIntegrity at exactly this offset, and older hosts stop at
// xShadowName.
static struct {
  sqlite3_module base;
  int (*xIntegrity)(sqlite3_vtab *pVTab, const char *zSchema,
                    const char *zTabName, int mFlags, char **pzErr);
} tg0ModuleWithIntegrity = {{TG0_MODULE_BASE_FIELDS},
                            /* xIntegrity    */ tg0Integrity};
#define tg0Module (*(sqlite3_module *)&tg0ModuleWithIntegrity)
#endif
#pragma endregion

#pragma region entrypoint

// SQLITE_RESULT_SUBTYPE was introduced in SQLite 3.45
#ifndef SQLITE_RESULT_SUBTYPE
#define SQLITE_RESULT_SUBTYPE 0x001000000
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

      // predicates
      {(char *)"tg_contains",       2, tg_predicate_impl,   tg_geom_contains,   NULL,         DEFAULT_FLAGS},
      {(char *)"tg_coveredby",      2, tg_predicate_impl,   tg_geom_coveredby,  NULL,         DEFAULT_FLAGS},
      {(char *)"tg_covers",         2, tg_predicate_impl,   tg_geom_covers,     NULL,         DEFAULT_FLAGS},
      {(char *)"tg_disjoint",       2, tg_predicate_impl,   tg_geom_disjoint,   NULL,         DEFAULT_FLAGS},
      {(char *)"tg_equals",         2, tg_predicate_impl,   tg_geom_equals,     NULL,         DEFAULT_FLAGS},
      {(char *)"tg_intersects",     2, tg_predicate_impl,   tg_geom_intersects, NULL,         DEFAULT_FLAGS},

      // measurements
      {(char *)"tg_distance_sphere", 2, tg_distance_sphere, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_touches",        2, tg_predicate_impl,   tg_geom_touches,    NULL,         DEFAULT_FLAGS},
      {(char *)"tg_within",         2, tg_predicate_impl,   tg_geom_within,     NULL,         DEFAULT_FLAGS},

      {(char *)"tg_geom",           1, tg_geom,                 NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_geom",           2, tg_geom,                 NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_type",           1, tg_type,                 NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_extra_json",     1, tg_extra_json,           NULL,             NULL,         DEFAULT_FLAGS | SQLITE_RESULT_SUBTYPE},

      {(char *)"tg_valid_geojson",  1, tg_valid_geojson,       NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_valid_wkb",      1, tg_valid_wkb,           NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_valid_wkt",      1, tg_valid_wkt,           NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_dims",                 1, tg_dims,                 NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_is_empty",             1, tg_is_empty,             NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_is_feature",           1, tg_is_feature,           NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_is_featurecollection", 1, tg_is_featurecollection, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_memsize",              1, tg_memsize,              NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_x",              1, tg_x,          NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_y",              1, tg_y,          NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_has_z",          1, tg_has_z,      NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_has_m",          1, tg_has_m,      NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_z",              1, tg_z,          NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_m",              1, tg_m,          NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_min_x",          1, tg_min_x,      NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_min_y",          1, tg_min_y,      NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_max_x",          1, tg_max_x,      NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_max_y",          1, tg_max_y,      NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_to_wkt",         1, tg_to_wkt,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_wkb",         1, tg_to_wkb,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_geojson",     1, tg_to_geojson, NULL,             NULL,         DEFAULT_FLAGS | SQLITE_RESULT_SUBTYPE},

      {(char *)"tg_geometrycollection", -1, tg_geometrycollection, NULL,        NULL,         DEFAULT_FLAGS},
      {(char *)"tg_line",               -1, tg_line,               NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_linestring",         -1, tg_line,               NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_multilinestring",    -1, tg_multilinestring,    NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_multipoint",         -1, tg_multipoint,         NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_multipolygon",       -1, tg_multipolygon,       NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point",               2, tg_point,              NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point",               3, tg_point,              NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point",               4, tg_point,              NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_polygon",            -1, tg_polygon,            NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_poly_exterior",       1, tg_poly_exterior_,     NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_rect",                2, tg_rect,               NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_rect",                4, tg_rect,               NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_ring",               -1, tg_ring,               NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_segment",             2, tg_segment,            NULL,       NULL,         DEFAULT_FLAGS},
      {(char *)"tg_segment",             4, tg_segment,            NULL,       NULL,         DEFAULT_FLAGS},
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
      .xEof = &pointsEachEof,
      .xResult = &pointsEachResult,
      .xFree = &pointsEachFree,
  };
  static struct control linesEach = {
      .name = "line",
      .xEof = &linesEachEof,
      .xResult = &linesEachResult,
      .xFree = &linesEachFree,
  };
  static struct control geometriesEach = {
      .name = "geometry",
      .xEof = &geometriesEachEof,
      .xResult = &geometriesEachResult,
      .xFree = &geometriesEachFree,
  };
  static struct control polygonsEach = {
      .name = "polygon",
      .xEof = &polygonsEachEof,
      .xResult = &polygonsEachResult,
      .xFree = &polygonsEachFree,
  };
  
  static struct control holesEach = {
      .name = "hole",
      .xEof = &holesEachEof,
      .xResult = &holesEachResult,
      .xFree = &holesEachFree,
  };

  static struct control linePointsEach = {
      .name = "point",
      .xEof = &linePointsEachEof,
      .xResult = &linePointsEachResult,
      .xFree = &linePointsEachFree,
      .xCheck = &lineEachCheck,
  };
  static struct control lineSegmentsEach = {
      .zColumns = "ax, ay, bx, by",
      .nCols = 4,
      .xEof = &lineSegmentsEachEof,
      .xResult = &lineSegmentsEachResult,
      .xFree = &lineSegmentsEachFree,
      .xCheck = &lineEachCheck,
  };
  static struct control ringPointsEach = {
      .name = "point",
      .xEof = &ringPointsEachEof,
      .xResult = &ringPointsEachResult,
      .xFree = &ringPointsEachFree,
      .xCheck = &ringEachCheck,
  };
  static struct control ringSegmentsEach = {
      .zColumns = "ax, ay, bx, by",
      .nCols = 4,
      .xEof = &ringSegmentsEachEof,
      .xResult = &ringSegmentsEachResult,
      .xFree = &ringSegmentsEachFree,
      .xCheck = &ringEachCheck,
  };

  static const struct {
    char *zModuleName;
    struct control *c;

  } aEachModules[] = {
      // clang-format off
      {(char *)"tg_points_each",  &pointsEach},
      {(char *)"tg_lines_each",  &linesEach},
      {(char *)"tg_polygons_each",  &polygonsEach},
      {(char *)"tg_holes_each",  &holesEach},
      {(char *)"tg_geometries_each",  &geometriesEach},
      {(char *)"tg_each",  &geometriesEach},
      {(char *)"tg_line_points_each",  &linePointsEach},
      {(char *)"tg_line_segments_each",  &lineSegmentsEach},
      {(char *)"tg_ring_points_each",  &ringPointsEach},
      {(char *)"tg_ring_segments_each",  &ringSegmentsEach},
      // clang-format on
  };

  for (int i = 0; i < sizeof(aEachModules) / sizeof(aEachModules[0]); i++) {
    rc = sqlite3_create_module(db, aEachModules[i].zModuleName,
                               &template_eachModule, aEachModules[i].c);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  
  static const struct {

    char *zFName;
    int nArg;
    void (*xStep)(sqlite3_context *, int, sqlite3_value **);
    void (*xFinal)(sqlite3_context *);

  } aGroup[] = {
    {"tg_group_bbox",                       1, tg_group_bbox_step,                tg_group_bbox_final},
    {"tg_group_line",                       1, tg_group_line_step,                tg_group_line_final},
    {"tg_group_multilinestring",            1, tg_group_multilinestring_step,     tg_group_multilinestring_final},
    {"tg_group_multipoint",                 1, tg_group_multipoint_step,          tg_group_multipoint_final},
    {"tg_group_multipolygon",               1, tg_group_multipolygon_step,        tg_group_multipolygon_final},
    {"tg_group_polygon",                    1, tg_group_polygon_step,             tg_group_polygon_final},
    {"tg_group_ring",                       1, tg_group_ring_step,                tg_group_ring_final},
    // "tg_group_geometrycollection" is the primary name (matches the
    // underscore-free tg_multipoint/tg_multilinestring/tg_multipolygon/
    // tg_geometrycollection naming convention). "tg_group_geometry_collection"
    // is kept registered as a deprecated compatibility alias — same
    // step/final pair, see docs.md.
    {"tg_group_geometrycollection",         1, tg_group_geometry_collection_step, tg_group_geometry_collection_final},
    {"tg_group_geometry_collection",        1, tg_group_geometry_collection_step, tg_group_geometry_collection_final},
    {"tg_group_feature_collection_geojson", 1, tg_feature_collection_step,        tg_feature_collection_final},
    {"tg_group_feature_collection_geojson", 2, tg_feature_collection_step,        tg_feature_collection_final},
  };

  for(int i = 0; i < sizeof(aGroup) / sizeof(aGroup[0]); i++) {
    rc = sqlite3_create_function_v2(
      db, 
      aGroup[i].zFName, 
      aGroup[i].nArg,
      SQLITE_UTF8 | SQLITE_INNOCUOUS, 
      NULL,
      NULL, 
      aGroup[i].xStep, 
      aGroup[i].xFinal, 
      NULL
    );
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

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

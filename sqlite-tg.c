#include "sqlite3ext.h"

SQLITE_EXTENSION_INIT1

#include "tg.h"
#include "sqlite-tg.h"
#include <ctype.h>
#include <errno.h>
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
// tg0 constructor parser
//
// Hand-rolled tokenizer + parsers for the arguments of
// `CREATE VIRTUAL TABLE ... USING tg0(...)`. Grammar:
//
//   column-def :=
//       name INTEGER PRIMARY KEY
//     | name TEXT PRIMARY KEY
//     | name geom-type [INDEXING "=" (NONE | NATURAL | YSTRIPES)]*
//     | name (TEXT | INT | INTEGER | FLOAT | DOUBLE | BLOB)
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
 * geometry column definition: `name <geom-type> [indexing=...]*`.
 *
 * out_column_name points into source (same lifetime). out_geom_type is 0 for
 * generic `geometry` columns or an enum tg_geom_type value for narrowed ones.
 * out_indexing is the tg_index to use when parsing stored geometries at query
 * time, TG_DEFAULT unless an `indexing=` option overrides it.
 * Returns SQLITE_EMPTY if not a geometry column definition, SQLITE_OK if it
 * is, or SQLITE_ERROR when it is one but has a bad option — in which case
 * *out_errmsg is set to an sqlite3_mprintf'd error message.
 */
static int tg0_parse_geometry_column_definition(
    const char *source, int source_length, char **out_column_name,
    int *out_column_name_length, int *out_geom_type,
    enum tg_index *out_indexing, char **out_errmsg) {
  struct Tg0Scanner scanner;
  struct Tg0Token token;
  struct Tg0Token key;
  char *column_name;
  int column_name_length;
  int geom_type;
  enum tg_index indexing = TG_DEFAULT;
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

  // optional `key=value` options. Only `indexing=none|natural|ystripes` is
  // recognized; a recognizable option shape with an unknown key or value is
  // a hard error, anything structurally off falls back to the caller's
  // generic could-not-parse error.
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
    } else {
      *out_errmsg = sqlite3_mprintf(
          TG0_CONSTRUCTOR_ERROR
          "unknown option '%.*s' in '%.*s', only 'indexing=' is supported on "
          "geometry columns",
          (int)(key.end - key.start), key.start, source_length, source);
      return SQLITE_ERROR;
    }
  }

  *out_column_name = column_name;
  *out_column_name_length = column_name_length;
  *out_geom_type = geom_type;
  *out_indexing = indexing;
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
      tg0_token_is(&token, "indexing")) {
    *out_errmsg = sqlite3_mprintf(
        TG0_CONSTRUCTOR_ERROR
        "'indexing=' is only supported on geometry columns, in '%.*s'",
        source_length, source);
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
};

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
// the columns declared so far. Returns NULL when the name is fine, or an
// sqlite3_mprintf'd error message.
static char *tg0_check_column_name(tg0_vtab *p, const char *arg,
                                   const char *name, int nName) {
  if (tg0_column_name_reserved(name, nName)) {
    return sqlite3_mprintf(TG0_CONSTRUCTOR_ERROR
                           "reserved column name '%.*s' in '%s'",
                           nName, name, arg);
  }
  if (tg0_column_name_taken(p, name, nName)) {
    return sqlite3_mprintf(TG0_CONSTRUCTOR_ERROR
                           "duplicate column name '%.*s' in '%s'",
                           nName, name, arg);
  }
  return NULL;
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
};

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
      *pzErr = tg0_check_column_name(pNew, arg, cName, nName);
      if (*pzErr) {
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
    rc = tg0_parse_geometry_column_definition(arg, nArg, &cName, &nName,
                                              &geomType, &indexing, &errmsg);
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
      *pzErr = tg0_check_column_name(pNew, arg, cName, nName);
      if (*pzErr) {
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
      *pzErr = tg0_check_column_name(pNew, arg, cName, nName);
      if (*pzErr) {
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

static int tg0Close(sqlite3_vtab_cursor *cur) {
  sqlite3_free((tg0_cursor *)cur);
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
        op == TG0_FUNC_COVERS || op == TG0_FUNC_COVEREDBY ||
        op == TG0_FUNC_TOUCHES || op == TG0_FUNC_EQUALS) {
      if (iPredicateTerm >= 0) {
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

// ---------------------------------------------------------------------------
// TEMPORARY read/write stubs. The old read/write paths were built around the
// single-rtree `_shape` shadow layout that the new column model replaces:
// upcoming changes rebuild the shadow tables, then the write path, then the
// read path. Until those land, scans and writes on tg0 tables report a clear
// error instead of running against a shadow layout that no longer exists.
// CREATE/DROP and the declared schema (PRAGMA table_xinfo) work fine.
// ---------------------------------------------------------------------------

#define TG0_TEMPORARILY_UNAVAILABLE                                            \
  "tg0 %s are temporarily unavailable while the tg0 rewrite lands"

static int tg0Filter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                     const char *idxStr, int argc, sqlite3_value **argv) {
  tg_vtab_set_error(pVtabCursor->pVtab, TG0_TEMPORARILY_UNAVAILABLE, "reads");
  return SQLITE_ERROR;
}

static int tg0Rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  *pRowid = 0;
  return SQLITE_ERROR;
}

static int tg0Next(sqlite3_vtab_cursor *cur) { return SQLITE_ERROR; }

static int tg0Eof(sqlite3_vtab_cursor *cur) { return 1; }

static int tg0Column(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                     int i) {
  sqlite3_result_null(context);
  return SQLITE_OK;
}

static int tg0Update(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                     sqlite_int64 *pRowid) {
  tg_vtab_set_error(pVTab, TG0_TEMPORARILY_UNAVAILABLE, "writes");
  return SQLITE_ERROR;
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

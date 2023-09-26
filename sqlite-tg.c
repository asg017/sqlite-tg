#include "sqlite3ext.h"

SQLITE_EXTENSION_INIT1

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tg.h>

// https://github.com/sqlite/sqlite/blob/2d3c5385bf168c85875c010bbaa79c6712eab214/src/json.c#L125-L126
#define JSON_SUBTYPE 74

#pragma region value

static const char *TG_GEOM_POINTER_NAME = "tg0-tg_geom";

const int32_t GPKG_APPID_MAGIC = 0x47504B47;  // "GPKG"
// http://www.geopackage.org/spec/#gpb_data_blob_format
const uint8_t GPKG_GEOM_MAGIC[] = {0x47, 0x50};
enum gpkg_envelope_type {
  GPKG_ENVELOPE_TYPE_NONE = 0,
  GPKG_ENVELOPE_TYPE_XY = 1,
  GPKG_ENVELOPE_TYPE_XYZ = 2,
  GPKG_ENVELOPE_TYPE_XYM = 3,
  GPKG_ENVELOPE_TYPE_XYZM = 4,
};
struct geopackage_geometry {
  uint8_t version;
  int binary_type;
  bool is_empty;
  enum gpkg_envelope_type envelope_type;
  int byte_order;
  int32_t srs_id;
  const double *envelope;
  const uint8_t *wkb;
  size_t wkb_size;
};
static bool is_gpkg_geometry(const uint8_t* blob) {
  return blob[0] == GPKG_GEOM_MAGIC[0] && blob[1] == GPKG_GEOM_MAGIC[1];
}

static bool parse_gpkg_geometry(const uint8_t* blob, size_t len, struct geopackage_geometry* geom) {
  // GeoPackageBinaryHeader
  if (len < 8) {
    sqlite3_log(SQLITE_ERROR, "parse_gpkg_geometry: too small");
    return false;
  }
  if (!is_gpkg_geometry(blob)) {
    sqlite3_log(SQLITE_ERROR, "parse_gpkg_geometry: bad magic number");
    return false;
  }

  geom->version = blob[2];
  if (!is_gpkg_geometry(blob)) {
    sqlite3_log(SQLITE_ERROR, "parse_gpkg_geometry: unsupported version: %d", geom->version);
    return false;
  }

  uint8_t flags = blob[3];
  geom->is_empty = (flags & 16) >> 4;
  geom->envelope_type = (flags & 14) >> 1;
  geom->binary_type = (flags & 32) >> 5;

  sqlite3_log(SQLITE_NOTICE, "parse_gpkg_geometry: flags=%#x", flags);

  if (geom->binary_type == 0) {
    // FIXME
    sqlite3_log(SQLITE_WARNING, "parse_gpkg_geometry: ignoring big-endian-ness");
  }
  geom->srs_id = *(int32_t*)&blob[4];

  size_t envelopeSize;
  switch (geom->envelope_type) {
    case GPKG_ENVELOPE_TYPE_XYZM:
      envelopeSize = 4 * sizeof(double) * 2;
      break;
    case GPKG_ENVELOPE_TYPE_XYM:
    case GPKG_ENVELOPE_TYPE_XYZ:
      envelopeSize = 3 * sizeof(double) * 2;
      break;
    case GPKG_ENVELOPE_TYPE_XY:
      envelopeSize = 2 * sizeof(double) * 2;
      break;
    case GPKG_ENVELOPE_TYPE_NONE:
      envelopeSize = 0;
      break;
    default:
      sqlite3_log(SQLITE_ERROR, "parse_gpkg_geometry: bad envelope type: %d", geom->envelope_type);
      return false;
  }
  if (len < 8 + envelopeSize) {
    // the blob is too small for this envelope
    sqlite3_log(SQLITE_ERROR, "parse_gpkg_geometry: header too small (2)");
    return false;
  }

  geom->envelope = (geom->envelope_type != GPKG_ENVELOPE_TYPE_NONE) ? (double*)&blob[8] : NULL;
  geom->wkb = blob + 8 + envelopeSize;
  geom->wkb_size = len - 8 - envelopeSize;
  return true;
}

static struct tg_geom *geomValue(sqlite3_value *value) {
  size_t n = sqlite3_value_bytes(value);
  switch (sqlite3_value_type(value)) {
  case SQLITE_BLOB: {
    const uint8_t *blob = sqlite3_value_blob(value);
    if (n >= 2 && is_gpkg_geometry(blob)) {
      // geopackage geometry
      struct geopackage_geometry gpkg_geom;
      if (!parse_gpkg_geometry(blob, n, &gpkg_geom)) {
        return NULL;
      }
      return tg_parse_wkb_ix(gpkg_geom.wkb, gpkg_geom.wkb_size, TG_NONE);
    } else {
      return tg_parse_wkb_ix(blob, n, TG_NONE);
    }
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

int is_geopackage_db(sqlite3 *db) {
  // run a query to determine whether the SQLite3 application_id field contains
  // the magic string
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, "PRAGMA application_id", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return -1;
  }

  int result = -1;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const int32_t application_id = sqlite3_column_int(stmt, 0);
    result = (application_id == GPKG_APPID_MAGIC);
  }

  sqlite3_finalize(stmt);
  return result;
}

#pragma endregion

#pragma resulting

static void resultGeomWkt(sqlite3_context *context, struct tg_geom *geom) {
  size_t size = tg_geom_wkt(geom, 0, 0);
  if (size == 0) {
    return;
  }

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

static void resultGeomGpkg(sqlite3_context *context, struct tg_geom *geom) {
  int ndims = tg_geom_dims(geom);
  bool isEmpty = tg_geom_is_empty(geom);

  size_t envelopeSize = 0;
  // tg bounding rects are only 2D
  if (!isEmpty) {
    envelopeSize = 2 * sizeof(double) * 2;
  }

  // GeoPackageBinaryHeader {
  //   byte[2] magic = 0x4750;
  //   byte version = 0;
  //   byte flags;
  //   int32 srs_id;
  //   double[] envelope;
  // }
  size_t headerSize = 2 + 1 + 1 + 4 + envelopeSize;
  size_t wkbSize = tg_geom_wkb(geom, 0, 0);
  size_t resultSize = wkbSize + headerSize;
  uint8_t *buffer = (uint8_t*)sqlite3_malloc(resultSize + 1);
  if (buffer == 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  tg_geom_wkb(geom, buffer + headerSize, wkbSize + 1);

  // TODO: match WKB endian-ness rather than encoding as native?
  uint8_t wkbByteOrder = (BYTE_ORDER == LITTLE_ENDIAN);
  buffer[0] = 0x47;
  buffer[1] = 0x50;
  buffer[2] = 0x00;
  // http://www.geopackage.org/spec/#flags_layout
  buffer[3] = 32 | (isEmpty << 4) | (!isEmpty << 1) | wkbByteOrder;
  // set srs_id to 0
  *(int32_t*)&buffer[4] = 0;

  if (!isEmpty) {
    struct tg_rect rect = tg_geom_rect(geom);
    double *envelope = (double *)(buffer + 8);
    envelope[0] = rect.min.x;
    envelope[1] = rect.max.x;
    envelope[2] = rect.min.y;
    envelope[3] = rect.max.y;
  }

  sqlite3_result_blob(context, buffer, resultSize, sqlite3_free);
  return;
}

enum export_format { WKT = 1, WKB = 2, GEOJSON = 3, POINTER = 4, GPKG = 5 };

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
  case GPKG: {
    resultGeomGpkg(context, geom);
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

static void tg_is_geopackage_db(sqlite3_context *context, int argc,
                             sqlite3_value **argv) {

  if (argc != 0) {
      sqlite3_result_error(context, "Invalid number of arguments. Expected 0.", -1);
      return;
  }

  int isGeopackage = is_geopackage_db(sqlite3_context_db_handle(context));
  if (isGeopackage < 0) {
    sqlite3_result_error(context, "Error checking GeoPackage application_id", -1);
    return;
  }
  sqlite3_result_int(context, isGeopackage);
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

static void tg_to_gpkg(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  struct tg_geom *geom = geomValue(argv[0]);
  if (tg_geom_error(geom)) {
    sqlite3_result_error(context, tg_geom_error(geom), -1);
    return;
  }
  resultGeomGpkg(context, geom);
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

// Minimum GeoPackage functions

// ST_IsEmpty(geom Geometry): integer
// Returns 1 if geometry value is empty, 0 if not empty, NULL if geometry value is NULL
static void ST_IsEmpty(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {

  if (argc != 1) {
      sqlite3_result_error(context, "Invalid number of arguments. Expected 1.", -1);
      return;
  }

  int n = sqlite3_value_bytes(argv[0]);
  if (sqlite3_value_type(argv[0]) == SQLITE_BLOB) {
    const uint8_t *blob = sqlite3_value_blob(argv[0]);
    if (n >= 2 && is_gpkg_geometry(blob)) {
      struct geopackage_geometry gpkg_geom;
      if (!parse_gpkg_geometry(blob, n, &gpkg_geom)) {
        sqlite3_result_error(context, "Expected GeoPackage binary geometry.", -1);
      }
      sqlite3_result_int(context, gpkg_geom.is_empty);
    }
  }
}

enum rect_vertex { MINX = 0, MAXX = 1, MINY = 2, MAXY = 3 };
static void _st_rect_vertex(sqlite3_context *context, int argc, sqlite3_value **argv, enum rect_vertex vertex) {
  if (argc != 1) {
    sqlite3_result_error(context, "Invalid number of arguments. Expected 1.", -1);
    return;
  }

  int n = sqlite3_value_bytes(argv[0]);
  if (sqlite3_value_type(argv[0]) == SQLITE_BLOB) {
    const uint8_t *blob = sqlite3_value_blob(argv[0]);
    if (n >= 2 && is_gpkg_geometry(blob)) {
      struct geopackage_geometry gpkg_geom;
      if (!parse_gpkg_geometry(blob, n, &gpkg_geom)) {
        sqlite3_result_error(context, "Expected GeoPackage binary geometry.", -1);
        return;
      }

      if (gpkg_geom.envelope_type != GPKG_ENVELOPE_TYPE_NONE) {
        sqlite3_result_double(context, gpkg_geom.envelope[vertex]);
      } else {
        struct tg_geom *geom = tg_parse_wkb_ix(gpkg_geom.wkb, gpkg_geom.wkb_size, TG_NONE);
        if (!geom) {
          sqlite3_result_error(context, "Error parsing WKB.", -1);
          return;
        }
        struct tg_rect rect = tg_geom_rect(geom);
        switch (vertex) {
          case MINX:
            sqlite3_result_double(context, rect.min.x);
            break;
          case MAXX:
            sqlite3_result_double(context, rect.max.x);
            break;
          case MINY:
            sqlite3_result_double(context, rect.min.y);
            break;
          case MAXY:
            sqlite3_result_double(context, rect.max.y);
            break;
        }

        tg_geom_free(geom);
      }
      return;
    }
  }
  sqlite3_result_error(context, "Expected GeoPackage binary geometry.", -1);
}

// ST_MinX(geom Geometry): real
// Returns the minimum X value of the bounding envelope of a geometry
static void ST_MinX(sqlite3_context *context, int argc,
                    sqlite3_value **argv) {
  _st_rect_vertex(context, argc, argv, MINX);
}

// ST_MaxX(geom Geometry): real
// Returns the maximum X value of the bounding envelope of a geometry
static void ST_MaxX(sqlite3_context *context, int argc,
                    sqlite3_value **argv) {
  _st_rect_vertex(context, argc, argv, MAXX);
}

// ST_MinY(geom Geometry): real
// Returns the minimum Y value of the bounding envelope of a geometry
static void ST_MinY(sqlite3_context *context, int argc,
                    sqlite3_value **argv) {
  _st_rect_vertex(context, argc, argv, MINY);
}

// ST_MaxY(geom Geometry): real
// Returns the maximum Y value of the bounding envelope of a geometry
static void ST_MaxY(sqlite3_context *context, int argc,
                    sqlite3_value **argv) {
  _st_rect_vertex(context, argc, argv, MAXY);
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
  static enum export_format FORMAT_GPKG = GPKG;

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
      {(char *)"tg_is_geopackage_db", 0, tg_is_geopackage_db, NULL,     NULL,         DEFAULT_FLAGS},

      {(char *)"tg_intersects",     2, tg_intersects, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_type",           1, tg_type,       NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_to_wkt",         1, tg_to_wkt,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_wkb",         1, tg_to_wkb,     NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_geojson",     1, tg_to_geojson, NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"tg_to_gpkg",        1, tg_to_gpkg,    NULL,             NULL,         DEFAULT_FLAGS},

      {(char *)"tg_point",          2, tg_point_impl, &FORMAT_POINTER,  NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_wkt",      2, tg_point_impl, &FORMAT_WKT,      NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_wkb",      2, tg_point_impl, &FORMAT_WKB,      NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_geojson",  2, tg_point_impl, &FORMAT_GEOJSON,  NULL,         DEFAULT_FLAGS},
      {(char *)"tg_point_gpkg",     2, tg_point_impl, &FORMAT_GPKG,     NULL,         DEFAULT_FLAGS},

      // GeoPackage required functions
      {(char *)"ST_IsEmpty",        1, ST_IsEmpty,    NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"ST_MinX",           1, ST_MinX,       NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"ST_MaxX",           1, ST_MaxX,       NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"ST_MinY",           1, ST_MinY,       NULL,             NULL,         DEFAULT_FLAGS},
      {(char *)"ST_MaxY",           1, ST_MaxY,       NULL,             NULL,         DEFAULT_FLAGS},

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

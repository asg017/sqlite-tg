# `sqlite-tg` Documentation

As a reminder, `sqlite-tg` is still young, so breaking changes should be expected while `sqlite-tg` is in a pre-v1 stage.

## Supported Formats

`tg` and `sqlite-tg` can accept geometries in [Well-known Text (WKT)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry), [Well-known Binary(WKB)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry#Well-known_binary), and [GeoJSON](https://geojson.org/) formats.

`sqlite-tg` functions will infer which format to use based on the following rules:

1. If a provided argument is a `BLOB`, then it is assumed the blob is valid WKB.
2. If the provided argument is `TEXT` and is the return value of a [JSON SQL function](https://www.sqlite.org/json1.html), or if it starts with `"{"`, then it is assumed the string is valid GeoJSON.
3. If the provided argument is still `TEXT`, then it is assumed the text is valid WKT.
4. If the provided argument is the return value of a `sqlite-tg` function that [returns a geometry pointer](#pointer-functions),

## Pointer functions

Some functions in `sqlite-tg` use SQLite's [Pointer Passsing Interface](https://www.sqlite.org/bindptr.html) to return special objects. This is mainly done for performance benefits in specific queries, to avoid the overhead serializing/de-serializing the same geometric object multiple times.

When using one of these functions it may appear to return `NULL`. Technically it is not null, but user-facing SQL queries can't directly access the real value. Instead, other `sqlite-tg` functions can read the underlying data in their own functions. For example:

```sql
select tg_point(1, 2);
-- NULL
select tg_to_wkt(tg_point(1, 2));
-- 'POINT(1 2)'
```

`tg_point` is a pointer function, which appears to return `NULL` when directly accessing in a query. However, it can be passed into other `sqlite-tg` functions, such as `tg_to_wkt()`, which access the underlying geometric object and serializes it to WKT.

Keep in mind, SQLite pointer values don't exist past CTE boundaries.

```sql
with step1 as (
  select tg_point(1,1) as point1
),
step2 as (
  select tg_to_wkt(point1) from step1
)
select * from step2;
-- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
```

The above query returns an error because the "pointer" returned from `tg_point()` inside `step1` doesn't exist outside the `step1` CTE boundary. When accessed in `step2`, the `point1` return is `NULL`, so `tg_to_wkt()` throws the error.

The solution is to "serialize" the point with `tg_to_wkt` inside of `step1`. This ensure that `point1` will be a normal SQL `TEXT` value, and can be queries in other table expressions like normal.

```sql
with step1 as (
  select tg_to_wkt(tg_point(1,1)) as point1
),
step2 as (
  select point1 from step1
)
select * from step2;
-- 'POINT(1 1)'
```

## API Reference

All functions offered by `sqlite-tg`.

### Meta Functions

#### `tg_version()` {#tg_version}

Returns the version string of `sqlite-tg`.

```sql
select tg_version();
-- 'v0.0.1-alpha.21'
```

#### `tg_debug()` {#tg_debug}

Returns fuller debug information of `sqlite-tg`, including the build date and commit of `sqlite-tg` and the vendored `tg` library.

```
select tg_debug();
-- 'sqlite-tg version: v0....
-- sqlite-tg date: ...
-- sqlite-tg commit: ...'
```

### Constructors

#### `tg_point(x, y)` {#tg_point}

A [pointer function](#pointer-functions) that returns a point geometry with the given `x` and `y` values. This value will appear to be `NULL` on direct access, and is meant for performance critical SQL queries where you want to avoid serializing/de-serializing.

`tg_point(x, y, z)` also accepts a third argument to construct a point with a Z coordinate, and `tg_point(x, y, z, m)` a fourth to also add an M coordinate. There is no way to construct an M-only point (with no Z) — use [`tg_geom()`](#tg_geom) with `POINT M (...)` WKT text instead.

```sql
select tg_to_wkt(tg_point(1, 2));
-- 'POINT(1 2)'
select tg_to_wkt(tg_point(1, 2, 3));
-- 'POINT(1 2 3)'
select tg_to_wkt(tg_point(1, 2, 3, 4));
-- 'POINT(1 2 3 4)'
```

#### `tg_line(p1, p2, ..., pN)` {#tg_line}

A [pointer function](#pointer-functions) that returns a LineString geometry made up of the given points, in order. Input arguments must be Point geometries, in any [supported format](#supported-formats).

```sql
select tg_to_wkt(tg_line(tg_point(0, 0), tg_point(1, 1), tg_point(2, 0)));
-- 'LINESTRING(0 0,1 1,2 0)'
```

#### `tg_linestring(p1, p2, ..., pN)` {#tg_linestring}

An alias of [`tg_line()`](#tg_line) — same implementation, provided for familiarity with the WKT `LINESTRING` type name. Note that any error messages it raises will still refer to `tg_line()`.

```sql
select tg_to_wkt(tg_linestring(tg_point(0, 0), tg_point(1, 1)));
-- 'LINESTRING(0 0,1 1)'
```

#### `tg_ring(p1, p2, ..., pN)` {#tg_ring}

A [pointer function](#pointer-functions) that returns a ring made up of the given points, in order. Input arguments must be Point geometries, in any [supported format](#supported-formats). A ring can always be used anywhere a Polygon or generic geometry is expected (see [`tg_poly_exterior()`](#tg_poly_exterior)), and is the input type expected by [`tg_polygon()`](#tg_polygon).

A ring is always treated as geometrically closed — the segment from the last point back to the first is implied even if you don't repeat it — but `tg_to_wkt()`/`tg_to_geojson()` output echoes back exactly the points you passed in. Repeat the first point as the last argument if you want conventional closed-ring WKT/GeoJSON text.

```sql
select tg_to_wkt(tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10)));
-- 'POLYGON((0 0,10 0,10 10))'
select tg_to_wkt(tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 0)));
-- 'POLYGON((0 0,10 0,10 10,0 0))'
```

#### `tg_polygon(exterior, hole1, ..., holeN)` {#tg_polygon}

A [pointer function](#pointer-functions) that returns a Polygon geometry from the given exterior ring and, optionally, one or more interior holes. Every argument must be a ring — either from [`tg_ring()`](#tg_ring)/[`tg_poly_exterior()`](#tg_poly_exterior)/[`tg_holes_each()`](#tg_holes_each), or a Polygon geometry with no holes of its own (its exterior ring is used). A Polygon argument that itself has holes is rejected, since silently using only its exterior would drop that data.

```sql
select tg_to_wkt(
  tg_polygon(
    tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 10), tg_point(0, 0))
  )
);
-- 'POLYGON((0 0,10 0,10 10,0 10,0 0))'
select tg_to_wkt(
  tg_polygon(
    tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 10), tg_point(0, 0)),
    tg_ring(tg_point(1, 1), tg_point(2, 1), tg_point(2, 2), tg_point(1, 2), tg_point(1, 1))
  )
);
-- 'POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))'
```

#### `tg_multipoint(p1, p2, ..., pN)` {#tg_multipoint}

A [pointer function](#pointer-functions) that returns a MultiPoint geometry with the given points. Input arguments must be Point geometries, in any [supported format](#supported-formats).

```sql
select tg_to_wkt(tg_multipoint(tg_point(1, 1), tg_point(2, 2)));
-- 'MULTIPOINT(1 1,2 2)'
```

#### `tg_multilinestring(l1, l2, ..., lN)` {#tg_multilinestring}

A [pointer function](#pointer-functions) that returns a MultiLineString geometry with the given LineStrings. Input arguments must be LineString geometries, in any [supported format](#supported-formats).

```sql
select tg_to_wkt(
  tg_multilinestring(
    tg_line(tg_point(0, 0), tg_point(1, 1)),
    tg_line(tg_point(2, 2), tg_point(3, 3))
  )
);
-- 'MULTILINESTRING((0 0,1 1),(2 2,3 3))'
```

#### `tg_multipolygon(p1, p2, ..., pN)` {#tg_multipolygon}

A [pointer function](#pointer-functions) that returns a MultiPolygon geometry with the given Polygons. Input arguments must be Polygon geometries (including bare rings, which upcast to Polygon), in any [supported format](#supported-formats).

```sql
select tg_to_wkt(
  tg_multipolygon(
    'POLYGON((0 0, 1 0, 1 1, 0 0))',
    'POLYGON((10 10, 11 10, 11 11, 10 10))'
  )
);
-- 'MULTIPOLYGON(((0 0,1 0,1 1,0 0)),((10 10,11 10,11 11,10 10)))'
```

#### `tg_geometrycollection(g1, g2, ..., gN)` {#tg_geometrycollection}

A [pointer function](#pointer-functions) that returns a GeometryCollection geometry with the given geometries, which may be of mixed types (including nested GeometryCollections).

```sql
select tg_to_wkt(
  tg_geometrycollection('POINT(1 1)', 'LINESTRING(0 0, 1 1)')
);
-- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))'
```

#### `tg_geom(input, $index)` {#tg_geom}

A [pointer function](#pointer-functions) that parses `input` (in any [supported format](#supported-formats)) into a geometry, optionally building an internal index. The optional second argument selects the [tg index type](https://github.com/tidwall/tg/blob/main/docs/POLYGON_INDEXING.md) and must be one of `'none'`, `'natural'`, or `'ystripes'`.

```sql
select tg_to_wkt(tg_geom('{"type":"Point","coordinates":[0,1]}'));
-- 'POINT(0 1)'
select tg_type(tg_geom('POLYGON((30 10, 40 40, 20 40, 10 20, 30 10))', 'ystripes'));
-- 'Polygon'
```

Unrecognized index options raise an error.

```sql
select tg_geom('POINT(0 1)', 'not-an-index');
-- error: unrecognized index option. Should be one of none/natural/ystripes
```

#### `tg_poly_exterior(polygon)` {#tg_poly_exterior}

A [pointer function](#pointer-functions) that returns the exterior ring of the given Polygon geometry, dropping any holes.

```sql
select tg_to_wkt(
  tg_poly_exterior('POLYGON((30 10, 40 40, 20 40, 10 20, 30 10), (20 30, 35 35, 30 20, 20 30))')
);
-- 'POLYGON((30 10,40 40,20 40,10 20,30 10))'
```

#### `tg_rect(minX, minY, maxX, maxY)` / `tg_rect(minP, maxP)` {#tg_rect}

A [pointer function](#pointer-functions) that returns the equivalent closed-ring Polygon geometry for the given rectangle: `POLYGON((minx miny, maxx miny, maxx maxy, minx maxy, minx miny))`, with counter-clockwise winding and the closing point repeated. `tg` has no `tg_geom` representation of a rect (see `struct tg_rect`), so this is how it composes with every other `sqlite-tg` function. The two-argument form takes Point geometries, in any [supported format](#supported-formats), for the min/max corners.

```sql
select tg_to_wkt(tg_rect(0, 0, 1, 1));
-- 'POLYGON((0 0,1 0,1 1,0 1,0 0))'
select tg_to_wkt(tg_rect(tg_point(0, 0), tg_point(1, 1)));
-- 'POLYGON((0 0,1 0,1 1,0 1,0 0))'
```

No normalization is applied: if `minX > maxX` (or `minY > maxY`), the polygon is built exactly as given, since neither `tg_ring_new()` nor `tg_poly_new()` validate point ordering.

```sql
select tg_to_wkt(tg_rect(1, 1, 0, 0));
-- 'POLYGON((1 1,0 1,0 0,1 0,1 1))'
```

Since the result is an ordinary Polygon geometry, it composes with predicates like any other geometry.

```sql
select tg_intersects(tg_rect(0, 0, 10, 10), tg_point(5, 5));
-- 1
```

#### `tg_segment(aX, aY, bX, bY)` / `tg_segment(aP, bP)` {#tg_segment}

A [pointer function](#pointer-functions) that returns the equivalent 2-point LineString geometry for the given segment: `LINESTRING(ax ay, bx by)`. `tg` has no `tg_geom` representation of a segment (see `struct tg_segment`), so this is how it composes with every other `sqlite-tg` function. The two-argument form takes Point geometries, in any [supported format](#supported-formats), for the two endpoints.

```sql
select tg_to_wkt(tg_segment(0, 0, 1, 1));
-- 'LINESTRING(0 0,1 1)'
select tg_to_wkt(tg_segment(tg_point(0, 0), tg_point(1, 1)));
-- 'LINESTRING(0 0,1 1)'
```

### Aggregate Constructors

These are [aggregate functions](https://www.sqlite.org/lang_aggfunc.html) that build a single geometry from a group of rows. Note that the order of aggregated rows is only guaranteed when the query has an explicit `order by`.

#### `tg_group_multipoint(point)` {#tg_group_multipoint}

A [pointer function](#pointer-functions) and aggregate function that builds a MultiPoint geometry from the aggregated Point geometries.

```sql
select tg_to_wkt(
  tg_group_multipoint(tg_point(value ->> 0, value ->> 1))
)
from json_each('[[1, 1], [2, 2], [3, 3]]');
-- 'MULTIPOINT(1 1,2 2,3 3)'
```

#### `tg_group_line(point)` {#tg_group_line}

A [pointer function](#pointer-functions) and aggregate function that builds a LineString geometry from the aggregated Point geometries, in row order. A zero-row aggregate produces `LINESTRING EMPTY`, matching [`tg_group_multipoint()`](#tg_group_multipoint).

```sql
select tg_to_wkt(
  tg_group_line(tg_point(value ->> 0, value ->> 1))
)
from json_each('[[1, 1], [2, 2]]');
-- 'LINESTRING(1 1,2 2)'
```

#### `tg_group_ring(point)` {#tg_group_ring}

A [pointer function](#pointer-functions) and aggregate function that builds a ring from the aggregated Point geometries, in row order — same closing-point behavior as [`tg_ring()`](#tg_ring). A zero-row aggregate produces the same empty-ring shape as `tg_ring()` with no arguments.

```sql
select tg_to_wkt(
  tg_group_ring(tg_point(value ->> 0, value ->> 1))
)
from json_each('[[0, 0], [10, 0], [10, 10]]');
-- 'POLYGON((0 0,10 0,10 10))'
```

#### `tg_group_polygon(ring)` {#tg_group_polygon}

A [pointer function](#pointer-functions) and aggregate function that builds a Polygon geometry from the aggregated rings: the first row is the exterior and any subsequent rows are holes. Each row follows the same ring convention as [`tg_polygon()`](#tg_polygon) — a bare ring upcast, or a hole-free Polygon geometry — and a Polygon-with-holes row is rejected for the same reason. A zero-row aggregate produces `POLYGON EMPTY`, matching `tg_polygon()` with no arguments. (The two-argument `tg_group_polygon(exterior, hole)` form sketched in earlier notes was deliberately not implemented — it composes awkwardly as an aggregate; use `tg_group_ring()` to build rings first if you need holes assembled separately.)

```sql
select tg_to_wkt(
  tg_group_polygon(value)
)
from json_each('[
  "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))",
  "POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))"
]');
-- 'POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))'
```

#### `tg_group_multilinestring(line)` {#tg_group_multilinestring}

A [pointer function](#pointer-functions) and aggregate function that builds a MultiLineString geometry from the aggregated LineString geometries.

```sql
select tg_to_wkt(
  tg_group_multilinestring(value)
)
from json_each('["LINESTRING(0 0, 1 1)", "LINESTRING(2 2, 3 3)"]');
-- 'MULTILINESTRING((0 0,1 1),(2 2,3 3))'
```

#### `tg_group_multipolygon(polygon)` {#tg_group_multipolygon}

A [pointer function](#pointer-functions) and aggregate function that builds a MultiPolygon geometry from the aggregated Polygon geometries.

```sql
select tg_to_wkt(
  tg_group_multipolygon(value)
)
from json_each('["POLYGON((0 0, 1 0, 1 1, 0 0))", "POLYGON((2 2, 3 2, 3 3, 2 2))"]');
-- 'MULTIPOLYGON(((0 0,1 0,1 1,0 0)),((2 2,3 2,3 3,2 2)))'
```

#### `tg_group_geometrycollection(geometry)` {#tg_group_geometrycollection}

A [pointer function](#pointer-functions) and aggregate function that builds a GeometryCollection from the aggregated geometries, which may be of mixed types. Named to match the underscore-free `tg_multipoint`/`tg_multilinestring`/`tg_multipolygon`/`tg_geometrycollection` convention.

```sql
select tg_to_wkt(
  tg_group_geometrycollection(value)
)
from json_each('["POINT(1 1)", "LINESTRING(0 0, 1 1)"]');
-- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))'
```

#### `tg_group_geometry_collection(geometry)` {#tg_group_geometry_collection}

A **deprecated** alias of [`tg_group_geometrycollection()`](#tg_group_geometrycollection) — same implementation, same behavior, kept registered for backwards compatibility. Prefer `tg_group_geometrycollection()` in new code.

```sql
select tg_to_wkt(
  tg_group_geometry_collection(value)
)
from json_each('["POINT(1 1)", "LINESTRING(0 0, 1 1)"]');
-- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))'
```

#### `tg_group_bbox(geometry)` {#tg_group_bbox}

A [pointer function](#pointer-functions) and aggregate function that returns the bounding box of all aggregated geometries, as a Polygon geometry.

```sql
select tg_to_wkt(
  tg_group_bbox(value)
)
from json_each('["POINT(1 1)", "POINT(5 9)"]');
-- 'POLYGON((1 1,5 1,5 9,1 9,1 1))'
```

#### `tg_group_feature_collection_geojson(geometry, $properties)` {#tg_group_feature_collection_geojson}

An aggregate function that builds a GeoJSON `FeatureCollection` string from the aggregated geometries. The optional second argument provides the `properties` object for each feature.

Note: this name keeps its underscores (`feature_collection`) rather than following the underscore-free `tg_group_geometrycollection`-style convention — `featurecollection` reads worse fused, and this isn't a `tg_geom` pointer-returning constructor like the others, so it was left as-is rather than renamed or aliased.

```sql
select tg_group_feature_collection_geojson(value)
from json_each('["POINT(1 1)", "POINT(2 2)"]');
-- '{"type":"FeatureCollection", "features": [{"type": "Feature", "geometry": {"type":"Point","coordinates":[1,1]}, "properties": {}},{"type": "Feature", "geometry": {"type":"Point","coordinates":[2,2]}, "properties": {}}]}'
```

```sql
select tg_group_feature_collection_geojson(
  value,
  json_object('idx', key)
)
from json_each('["POINT(1 1)", "POINT(2 2)"]');
-- '{"type":"FeatureCollection", "features": [{"type": "Feature", "geometry": {"type":"Point","coordinates":[1,1]}, "properties": {"idx":0}},{"type": "Feature", "geometry": {"type":"Point","coordinates":[2,2]}, "properties": {"idx":1}}]}'
```

### Conversions

#### `tg_to_geojson(geometry)` {#tg_to_geojson}

Converts the given geometry into a GeoJSON string. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_geojson()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_geojson).

```sql
select tg_to_geojson('POINT(0 1)');
-- '{"type":"Point","coordinates":[0,1]}'
select tg_to_geojson(X'01010000000000000000000000000000000000f03f');
-- '{"type":"Point","coordinates":[0,1]}'
select tg_to_geojson('{"type":"Point","coordinates":[0,1]}');
-- '{"type":"Point","coordinates":[0,1]}'
select tg_to_geojson(tg_point(0, 1));
-- '{"type":"Point","coordinates":[0,1]}'
```

#### `tg_to_wkb(geometry)` {#tg_to_wkb}

Converts the given geometry into a WKB blob. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_wkb()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_wkb).

```sql
select tg_to_wkb('POINT(0 1)');
-- X'01010000000000000000000000000000000000F03F'
select tg_to_wkb(X'01010000000000000000000000000000000000f03f');
-- X'01010000000000000000000000000000000000F03F'
select tg_to_wkb('{"type":"Point","coordinates":[0,1]}');
-- X'01010000000000000000000000000000000000F03F'
select tg_to_wkb(tg_point(0, 1));
-- X'01010000000000000000000000000000000000F03F'
```

#### `tg_to_wkt(geometry)` {#tg_to_wkt}

Converts the given geometry into a WKT string. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_wkt()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_wkt).

```sql
select tg_to_wkt('POINT(0 1)');
-- 'POINT(0 1)'
select tg_to_wkt(X'01010000000000000000000000000000000000f03f');
-- 'POINT(0 1)'
select tg_to_wkt('{"type":"Point","coordinates":[0,1]}');
-- 'POINT(0 1)'
select tg_to_wkt(tg_point(0, 1));
-- 'POINT(0 1)'
```

### Misc.

#### `tg_type(geometry)` {#tg_type}

Returns a string describing the type of the provided `geometry`. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_type_string()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_type_string).

Possible values:

- `"Point"`
- `"LineString"`
- `"Polygon"`
- `"MultiPoint"`
- `"MultiLineString"`
- `"MultiPolygon"`
- `"GeometryCollection"`
- `"Unknown"`

```sql
select tg_type('POINT (30 10)');
-- 'Point'
select tg_type('LINESTRING (30 10, 10 30, 40 40)');
-- 'LineString'
select tg_type('POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))');
-- 'Polygon'
select tg_type('MULTIPOINT (10 40, 40 30, 20 20, 30 10)');
-- 'MultiPoint'
select tg_type('MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))');
-- 'MultiPolygon'
select tg_type('GEOMETRYCOLLECTION (POINT (40 10),LINESTRING (10 10, 20 20, 10 40),POLYGON ((40 40, 20 45, 45 30, 40 40)))');
-- 'GeometryCollection'
```

#### `tg_extra_json(geometry)` {#tg_extra_json}

If the original geometry is a GeoJSON with extra fields such as `id` or `property`, those extra fields will be returned in a JSON object. Returns `NULL` if there are no extra fields.

```sql
select tg_extra_json('{
  "type": "Point",
  "coordinates": [-118.2097812,34.0437074]
}');
-- NULL
select tg_extra_json('{
  "id": "ASG0017",
  "type": "Point",
  "coordinates": [-118.2097812,34.0437074],
  "properties": {"color": "red"}
}');
-- '{"id":"ASG0017","properties":{"color": "red"}}'
```

#### `tg_dims(geometry)` {#tg_dims}

Returns the number of coordinate dimensions of the given `geometry`: `2` for plain X/Y geometries, `3` when it carries a Z or M coordinate, and `4` when it carries both. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_dims()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_dims).

```sql
select tg_dims('POINT(0 1)');
-- 2
```

#### `tg_is_empty(geometry)` {#tg_is_empty}

Returns `1` if the given `geometry` is an empty geometry (such as `POINT EMPTY`), `0` otherwise. Inputs can be in [any supported formats](#supported-formats), including WKT, WKB, and GeoJSON. Based on [`tg_geom_is_empty()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_is_empty).

```sql
select tg_is_empty('POINT EMPTY');
-- 1
select tg_is_empty('POINT(0 1)');
-- 0
```

#### `tg_is_feature(geometry)` {#tg_is_feature}

Returns `1` if the given `geometry` was parsed from a GeoJSON `Feature`, `0` otherwise. Based on [`tg_geom_is_feature()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_is_feature).

```sql
select tg_is_feature('{"type":"Feature","geometry":{"type":"Point","coordinates":[0,1]},"properties":{}}');
-- 1
select tg_is_feature('{"type":"Point","coordinates":[0,1]}');
-- 0
```

#### `tg_is_featurecollection(geometry)` {#tg_is_featurecollection}

Returns `1` if the given `geometry` was parsed from a GeoJSON `FeatureCollection`, `0` otherwise. Based on [`tg_geom_is_featurecollection()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_is_featurecollection).

```sql
select tg_is_featurecollection('{"type":"FeatureCollection","features":[]}');
-- 1
select tg_is_featurecollection('{"type":"Point","coordinates":[0,1]}');
-- 0
```

#### `tg_memsize(geometry)` {#tg_memsize}

Returns the number of bytes of memory used to store the given `geometry` in its parsed, in-memory representation. Useful for estimating the cost of indexed blobs, such as those created with `tg_geom(x, 'ystripes')`. Based on [`tg_geom_memsize()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_memsize).

```sql
select tg_memsize('POINT(0 1)');
-- 24
```

#### `tg_valid_geojson(text)` {#tg_valid_geojson}

Returns `1` if the given text is valid GeoJSON, `0` otherwise.

```sql
select tg_valid_geojson('{"type":"Point","coordinates":[0,1]}');
-- 1
select tg_valid_geojson('{"type": "not-geojson"}');
-- 0
```

#### `tg_valid_wkt(text)` {#tg_valid_wkt}

Returns `1` if the given text is valid WKT, `0` otherwise.

```sql
select tg_valid_wkt('POINT(0 1)');
-- 1
select tg_valid_wkt('POINT()');
-- 0
```

#### `tg_valid_wkb(blob)` {#tg_valid_wkb}

Returns `1` if the given blob is valid WKB, `0` otherwise.

```sql
select tg_valid_wkb(X'01010000000000000000000000000000000000f03f');
-- 1
select tg_valid_wkb(X'00');
-- 0
```

### Coordinates and bounding boxes

Coordinate and bounding box accessors.

#### `tg_x(point)` {#tg_x}

Returns the X coordinate of the given Point geometry. Raises an error if the input is not a Point.

```sql
select tg_x('POINT(1 2)');
-- 1.0
select tg_x('LINESTRING(0 0, 1 1)');
-- error: tg_x() expected a Point geometry, found LineString
```

#### `tg_y(point)` {#tg_y}

Returns the Y coordinate of the given Point geometry. Raises an error if the input is not a Point.

```sql
select tg_y('POINT(1 2)');
-- 2.0
select tg_y('LINESTRING(0 0, 1 1)');
-- error: tg_y() expected a Point geometry, found LineString
```

#### `tg_has_z(geometry)` {#tg_has_z}

Returns `1` if the given geometry has a Z coordinate, `0` otherwise. Works on any geometry type, not just Points.

```sql
select tg_has_z('POINT(1 2)');
-- 0
select tg_has_z('POINT Z (1 2 3)');
-- 1
select tg_has_z('LINESTRING Z (0 0 1, 1 1 1)');
-- 1
```

#### `tg_has_m(geometry)` {#tg_has_m}

Returns `1` if the given geometry has an M coordinate, `0` otherwise. Works on any geometry type, not just Points.

```sql
select tg_has_m('POINT(1 2)');
-- 0
select tg_has_m('POINT ZM (1 2 3 4)');
-- 1
```

#### `tg_z(point)` {#tg_z}

Returns the Z coordinate of the given Point geometry, or `NULL` if it has no Z coordinate. Raises an error if the input is not a Point. `NULL` is used instead of `0` for a missing Z so it's distinguishable from an actual Z value of `0` — check with [`tg_has_z()`](#tg_has_z) if you need to tell "no Z" apart from "Z is 0".

```sql
select tg_z('POINT(1 2)');
-- NULL
select tg_z('POINT Z (1 2 3)');
-- 3.0
select tg_z('LINESTRING(0 0, 1 1)');
-- error: tg_z() expected a Point geometry, found LineString
```

#### `tg_m(point)` {#tg_m}

Returns the M coordinate of the given Point geometry, or `NULL` if it has no M coordinate. Raises an error if the input is not a Point. Same `NULL`-for-missing convention as [`tg_z()`](#tg_z).

```sql
select tg_m('POINT(1 2)');
-- NULL
select tg_m('POINT ZM (1 2 3 4)');
-- 4.0
select tg_m('LINESTRING(0 0, 1 1)');
-- error: tg_m() expected a Point geometry, found LineString
```

#### `tg_min_x(geometry)` {#tg_min_x}

Returns the minimum X coordinate of the given geometry's bounding box. Based on [`tg_geom_rect()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_rect). Useful for populating an [R-Tree](https://www.sqlite.org/rtree.html) index alongside a geometry column.

```sql
select tg_min_x('POLYGON((1 1, 2 2, 3 3, 1 1))');
-- 1.0
```

#### `tg_min_y(geometry)` {#tg_min_y}

Returns the minimum Y coordinate of the given geometry's bounding box. Based on [`tg_geom_rect()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_rect).

```sql
select tg_min_y('POLYGON((1 1, 2 2, 3 3, 1 1))');
-- 1.0
```

#### `tg_max_x(geometry)` {#tg_max_x}

Returns the maximum X coordinate of the given geometry's bounding box. Based on [`tg_geom_rect()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_rect).

```sql
select tg_max_x('POLYGON((1 1, 2 2, 3 3, 1 1))');
-- 3.0
```

#### `tg_max_y(geometry)` {#tg_max_y}

Returns the maximum Y coordinate of the given geometry's bounding box. Based on [`tg_geom_rect()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_rect).

```sql
select tg_max_y('POLYGON((1 1, 2 2, 3 3, 1 1))');
-- 3.0
```

### Operations

Every predicate accepts two geometries `a` and `b`, in any [supported format](#supported-formats), and returns `1` or `0`. All raise an error if either input is not a valid geometry.

#### `tg_intersects(a, b)` {#tg_intersects}

Returns `1` if the `a` geometry intersects the `b` geometry, otherwise returns `0`. Based on [`tg_geom_intersects()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_intersects).

```sql
select tg_intersects(
  'LINESTRING (0 0, 2 2)',
  'LINESTRING (1 0, 1 2)'
);
-- 1
select tg_intersects(
  'LINESTRING (0 0, 0 2)',
  'LINESTRING (2 0, 2 2)'
);
-- 0
```

Consider this rough bounding box for San Francisco:

```
POLYGON((
  -122.51610563264538 37.81424532146113,
  -122.51610563264538 37.69618409220847,
  -122.35290547288255 37.69618409220847,
  -122.35290547288255 37.81424532146113,
  -122.51610563264538 37.81424532146113
))
```

The following SQL query, for a point within the city, returns `1`:

```sql
select tg_intersects(
  '
    POLYGON((
      -122.51610563264538 37.81424532146113,
      -122.51610563264538 37.69618409220847,
      -122.35290547288255 37.69618409220847,
      -122.35290547288255 37.81424532146113,
      -122.51610563264538 37.81424532146113
    ))
  ',
  'POINT(-122.4075 37.787994)'
) as result;
-- 1
```

With a point outside the city it returns `0`:

```sql
select tg_intersects(
  '
    POLYGON((
      -122.51610563264538 37.81424532146113,
      -122.51610563264538 37.69618409220847,
      -122.35290547288255 37.69618409220847,
      -122.35290547288255 37.81424532146113,
      -122.51610563264538 37.81424532146113
    ))
  ',
  'POINT(-73.985130 40.758896)'
) as result;
-- 0
```

#### `tg_disjoint(a, b)` {#tg_disjoint}

Returns `1` if the `a` geometry shares no point with the `b` geometry, otherwise returns `0`. The inverse of [`tg_intersects`](#operations). Based on [`tg_geom_disjoint()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_disjoint).

```sql
select tg_disjoint('POINT(5 5)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 1
select tg_disjoint('POINT(1 1)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 0
```

#### `tg_contains(a, b)` {#tg_contains}

Returns `1` if the `b` geometry is fully contained inside the `a` geometry, otherwise returns `0`. Like [`tg_within`](#operations) with the arguments swapped, but with slightly stricter "boundary" rules — a geometry lying only on the boundary of `a` is not "contained". Based on [`tg_geom_contains()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_contains).

```sql
select tg_contains('POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))', 'POINT(1 1)');
-- 1
select tg_contains('POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))', 'POINT(0 1)');
-- 0
```

#### `tg_within(a, b)` {#tg_within}

Returns `1` if the `a` geometry is fully contained inside the `b` geometry, otherwise returns `0`. Like [`tg_contains`](#operations) with the arguments swapped. Based on [`tg_geom_within()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_within).

```sql
select tg_within('POINT(1 1)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 1
select tg_within('POINT(5 5)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 0
```

#### `tg_covers(a, b)` {#tg_covers}

Returns `1` if the `a` geometry covers every point of the `b` geometry (boundary points included), otherwise returns `0`. Based on [`tg_geom_covers()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_covers).

```sql
select tg_covers('POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))', 'POINT(0 1)');
-- 1
select tg_covers('POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))', 'POINT(5 5)');
-- 0
```

#### `tg_coveredby(a, b)` {#tg_coveredby}

Returns `1` if every point of the `a` geometry (boundary points included) is covered by the `b` geometry, otherwise returns `0`. Like [`tg_covers`](#operations) with the arguments swapped. Based on [`tg_geom_coveredby()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_coveredby).

```sql
select tg_coveredby('POINT(0 1)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 1
select tg_coveredby('POINT(5 5)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 0
```

#### `tg_touches(a, b)` {#tg_touches}

Returns `1` if the `a` geometry touches the `b` geometry — they share at least one point, but their interiors do not intersect. Otherwise returns `0`. Based on [`tg_geom_touches()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_touches).

```sql
select tg_touches('POINT(0 1)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 1
select tg_touches('POINT(1 1)', 'POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))');
-- 0
```

#### `tg_equals(a, b)` {#tg_equals}

Returns `1` if the `a` geometry is spatially equal to the `b` geometry, otherwise returns `0`. Based on [`tg_geom_equals()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_equals).

```sql
select tg_equals('POINT(1 1)', 'POINT(1 1)');
-- 1
select tg_equals('POINT(1 1)', 'POINT(2 2)');
-- 0
```

### Table Functions

Each of these table functions iterates over the components of a single geometry. The geometry-valued columns are [pointer values](#pointer-functions), so serialize them with `tg_to_wkt()` and friends to read them. `rowid` is the zero-based index of the component.

#### `tg_points_each(geometry)` {#tg_points_each}

Iterates over every Point in the given MultiPoint geometry, in the `point` column.

```sql
select rowid, tg_to_wkt(point) as point
from tg_points_each('MULTIPOINT (10 40, 40 30, 20 20)');
/*
┌───────┬────────────────┐
│ rowid │ point          │
├───────┼────────────────┤
│ 0     │ 'POINT(10 40)' │
│ 1     │ 'POINT(40 30)' │
│ 2     │ 'POINT(20 20)' │
└───────┴────────────────┘
*/
```

#### `tg_lines_each(geometry)` {#tg_lines_each}

Iterates over every LineString in the given MultiLineString geometry, in the `line` column.

```sql
select rowid, tg_to_wkt(line) as line
from tg_lines_each('MULTILINESTRING ((10 10, 20 20), (40 40, 30 30))');
/*
┌───────┬───────────────────────────┐
│ rowid │ line                      │
├───────┼───────────────────────────┤
│ 0     │ 'LINESTRING(10 10,20 20)' │
│ 1     │ 'LINESTRING(40 40,30 30)' │
└───────┴───────────────────────────┘
*/
```

#### `tg_line_points_each(line)` {#tg_line_points_each}

Iterates over every Point of the given LineString geometry, in the `point` column. Errors on non-LineString input.

```sql
select rowid, tg_to_wkt(point) as point
from tg_line_points_each('LINESTRING (10 10, 20 20, 10 40)');
/*
┌───────┬────────────────┐
│ rowid │ point          │
├───────┼────────────────┤
│ 0     │ 'POINT(10 10)' │
│ 1     │ 'POINT(20 20)' │
│ 2     │ 'POINT(10 40)' │
└───────┴────────────────┘
*/
```

#### `tg_line_segments_each(line)` {#tg_line_segments_each}

Iterates over every segment of the given LineString geometry, as flat `ax`, `ay`, `bx`, `by` columns. Errors on non-LineString input.

```sql
select rowid, ax, ay, bx, by
from tg_line_segments_each('LINESTRING (10 10, 20 20, 10 40)');
/*
┌───────┬──────┬──────┬──────┬──────┐
│ rowid │ ax   │ ay   │ bx   │ by   │
├───────┼──────┼──────┼──────┼──────┤
│ 0     │ 10.0 │ 10.0 │ 20.0 │ 20.0 │
│ 1     │ 20.0 │ 20.0 │ 10.0 │ 40.0 │
└───────┴──────┴──────┴──────┴──────┘
*/
```

#### `tg_polygons_each(geometry)` {#tg_polygons_each}

Iterates over every Polygon in the given MultiPolygon geometry, in the `polygon` column.

```sql
select rowid, tg_to_wkt(polygon) as polygon
from tg_polygons_each('MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 15 5)))');
/*
┌───────┬──────────────────────────────────────┐
│ rowid │ polygon                              │
├───────┼──────────────────────────────────────┤
│ 0     │ 'POLYGON((30 20,45 40,10 40,30 20))' │
│ 1     │ 'POLYGON((15 5,40 10,10 20,15 5))'   │
└───────┴──────────────────────────────────────┘
*/
```

#### `tg_holes_each(polygon)` {#tg_holes_each}

Iterates over every hole (interior ring) of the given Polygon geometry, in the `hole` column.

```sql
select rowid, tg_to_wkt(hole) as hole
from tg_holes_each('POLYGON(
  (0 0, 10 0, 10 10, 0 10, 0 0),
  (1 1, 2 1, 2 2, 1 2, 1 1),
  (5 5, 6 5, 6 6, 5 6, 5 5)
)');
/*
┌───────┬──────────────────────────────────┐
│ rowid │ hole                             │
├───────┼──────────────────────────────────┤
│ 0     │ 'POLYGON((1 1,2 1,2 2,1 2,1 1))' │
│ 1     │ 'POLYGON((5 5,6 5,6 6,5 6,5 5))' │
└───────┴──────────────────────────────────┘
*/
```

#### `tg_ring_points_each(ring)` {#tg_ring_points_each}

Iterates over every Point of the given ring, in the `point` column. The ring argument accepts either the upcast ring pointers produced by [`tg_poly_exterior()`](#tg_poly_exterior)/[`tg_holes_each()`](#tg_holes_each), or a plain Polygon geometry, in which case its exterior ring is used. Errors on any other geometry type.

```sql
select rowid, tg_to_wkt(point) as point
from tg_ring_points_each(tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))'));
/*
┌───────┬────────────────┐
│ rowid │ point          │
├───────┼────────────────┤
│ 0     │ 'POINT(0 0)'   │
│ 1     │ 'POINT(10 0)'  │
│ 2     │ 'POINT(10 10)' │
│ 3     │ 'POINT(0 10)'  │
│ 4     │ 'POINT(0 0)'   │
└───────┴────────────────┘
*/
```

#### `tg_ring_segments_each(ring)` {#tg_ring_segments_each}

Iterates over every segment of the given ring, as flat `ax`, `ay`, `bx`, `by` columns. Accepts the same ring/Polygon input as [`tg_ring_points_each()`](#tg_ring_points_each).

```sql
select rowid, ax, ay, bx, by
from tg_ring_segments_each(tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))'));
/*
┌───────┬──────┬──────┬──────┬──────┐
│ rowid │ ax   │ ay   │ bx   │ by   │
├───────┼──────┼──────┼──────┼──────┤
│ 0     │ 0.0  │ 0.0  │ 10.0 │ 0.0  │
│ 1     │ 10.0 │ 0.0  │ 10.0 │ 10.0 │
│ 2     │ 10.0 │ 10.0 │ 0.0  │ 10.0 │
│ 3     │ 0.0  │ 10.0 │ 0.0  │ 0.0  │
└───────┴──────┴──────┴──────┴──────┘
*/
```

#### `tg_geometries_each(geometry)` {#tg_geometries_each}

Iterates over every geometry in the given GeometryCollection, in the `geometry` column.

```sql
select rowid, tg_to_wkt(geometry) as geometry
from tg_geometries_each('GEOMETRYCOLLECTION (POINT (40 10), LINESTRING (10 10, 20 20, 10 40))');
/*
┌───────┬─────────────────────────────────┐
│ rowid │ geometry                        │
├───────┼─────────────────────────────────┤
│ 0     │ 'POINT(40 10)'                  │
│ 1     │ 'LINESTRING(10 10,20 20,10 40)' │
└───────┴─────────────────────────────────┘
*/
```

#### `tg_each(geometry)` {#tg_each}

An alias of [`tg_geometries_each`](#table-functions).

```sql
select rowid, tg_to_wkt(geometry) as geometry
from tg_each('GEOMETRYCOLLECTION (POINT (40 10), LINESTRING (10 10, 20 20, 10 40))');
/*
┌───────┬─────────────────────────────────┐
│ rowid │ geometry                        │
├───────┼─────────────────────────────────┤
│ 0     │ 'POINT(40 10)'                  │
│ 1     │ 'LINESTRING(10 10,20 20,10 40)' │
└───────┴─────────────────────────────────┘
*/
```

#### `tg_bbox(geometry)` {#tg_bbox}

Returns a single row with the bounding box of the given geometry, in the `minX`, `maxX`, `minY`, and `maxY` columns.

```sql
select * from tg_bbox('LINESTRING (30 10, 10 30, 40 40)');
/*
┌──────┬──────┬──────┬──────┐
│ minX │ maxX │ minY │ maxY │
├──────┼──────┼──────┼──────┤
│ 10.0 │ 40.0 │ 10.0 │ 40.0 │
└──────┴──────┴──────┴──────┘
*/
```

### Virtual Tables

#### `tg0(...)` {#tg0}

An experimental virtual table for querying large geospatial datasets. A `tg0` table stores rows in an ordinary shadow table and automatically maintains one [R-Tree index](https://www.sqlite.org/rtree.html) of bounding boxes per geometry column, so spatial predicate queries only parse and test the geometries whose bounding boxes qualify. Requires the R-Tree extension to be compiled into the host SQLite. Still experimental — expect breaking changes (KNN queries and partition keys are planned).

The constructor takes a list of typed column definitions, in any order:

```
create virtual table name using tg0(
  -- optional primary key, at most one:
  id integer primary key,      -- or: id text primary key

  -- 1 to 8 geometry columns:
  shape geometry,              -- or one of the 7 concrete types:
                               --   point, linestring, polygon, multipoint,
                               --   multilinestring, multipolygon,
                               --   geometrycollection
                               -- with an optional per-column option:
                               --   indexing=none|natural|ystripes

  -- 0 to 16 data columns:
  label text                   -- text | int | integer | float | double | blob
);
```

- **Primary key** (optional): `id integer primary key` aliases the rowid, like an ordinary SQLite table. `id text primary key` declares a `WITHOUT ROWID` table keyed by text ids. With no primary key declared, the table has plain rowids.
- **Geometry columns** (1 to 8): `geometry` accepts any geometry type, while the 7 concrete type keywords narrow the column to exactly that type. Geometry columns are `NOT NULL` and strictly typed — inserting `NULL` or inserting a `Polygon` into a `multipolygon` column raises a constraint error, with no implicit promotion. The optional `indexing=none|natural|ystripes` option picks the [tg polygon index](https://github.com/tidwall/tg/blob/main/docs/POLYGON_INDEXING.md) used when stored geometries are parsed at query time.
- **Data columns** (0 to 16): plain values stored alongside the geometries. Nullable, and type-enforced on write: a type mismatch raises a constraint error, except integers are accepted into `float` columns.

Inserted geometries can be in [any supported format](#supported-formats); reading a geometry column always returns WKB.

```sql
create virtual table businesses using tg0(
  id integer primary key,
  name text,
  location point
);
insert into businesses(id, name, location) values
  (1, 'in sf', 'POINT(-122.4075 37.787994)'),
  (2, 'in nyc', '{"type":"Point","coordinates":[-73.985130,40.758896]}');
select id, name
from businesses
where tg_intersects(location, 'POLYGON((
  -122.51610563264538 37.81424532146113,
  -122.51610563264538 37.69618409220847,
  -122.35290547288255 37.69618409220847,
  -122.35290547288255 37.81424532146113,
  -122.51610563264538 37.81424532146113
))');
/*
┌────┬─────────┐
│ id │ name    │
├────┼─────────┤
│ 1  │ 'in sf' │
└────┴─────────┘
*/
```

All 8 predicate functions are accelerated when called on a geometry column in a `WHERE` clause:

| predicate                                  | R-Tree prefilter                |
| ------------------------------------------ | ------------------------------- |
| `tg_intersects`, `tg_touches`, `tg_equals` | bounding boxes overlap          |
| `tg_contains`, `tg_covers`                 | stored bbox contains query bbox |
| `tg_within`, `tg_coveredby`                | stored bbox within query bbox   |
| `tg_disjoint`                              | none — full scan + exact test   |

Prefiltered candidates are always re-checked with the exact predicate, so results are identical to a full scan. Multiple predicates in one `WHERE` clause are fine: one drives the R-Tree plan and the rest run as ordinary scalar functions. `rowid =` or primary key lookups become point queries, and `=`, `!=`, `<`, `<=`, `>`, `>=` constraints on data columns are pushed down into the content-table scan. `EXPLAIN QUERY PLAN` shows which plan a query uses:

```sql
explain query plan
select id, name
from businesses
where tg_intersects(location, 'POLYGON((0 0, 1 0, 1 1, 0 0))');
/*
┌────┬────────┬─────────┬───────────────────────────────────────────────┐
│ id │ parent │ notused │ detail                                        │
├────┼────────┼─────────┼───────────────────────────────────────────────┤
│ 2  │ 0      │ 49      │ 'SCAN businesses VIRTUAL TABLE INDEX 0:3PAIO' │
└────┴────────┴─────────┴───────────────────────────────────────────────┘
*/
```

`UPDATE` is supported for geometry and data columns; the R-Tree entries follow along. Changing a row's rowid or primary key with `UPDATE` is not supported. `ALTER TABLE ... RENAME TO` and `PRAGMA integrity_check` work as expected.

```sql
update businesses set location = 'POINT(-73.9855 40.758)' where id = 2;
select id, tg_to_wkt(location) as location from businesses;
/*
┌────┬──────────────────────────────┐
│ id │ location                     │
├────┼──────────────────────────────┤
│ 1  │ 'POINT(-122.4075 37.787994)' │
│ 2  │ 'POINT(-73.9855 40.758)'     │
└────┴──────────────────────────────┘
*/
```

Text primary keys are handy for datasets with natural string identifiers:

```sql
create virtual table timezones using tg0(
  tzid text primary key,
  boundary multipolygon
);
insert into timezones(tzid, boundary) values
  ('America/Los_Angeles', 'MULTIPOLYGON(((-125 32, -114 32, -114 42, -125 42, -125 32)))');
select tzid from timezones
where tg_intersects(boundary, 'POINT(-118.2437 34.0522)');
-- 'America/Los_Angeles'
```

A hidden `distance` column reserves the surface for KNN ("k nearest neighbors") queries, which are **not implemented yet**: `MATCH` or `distance` constraints currently error with `KNN queries are not supported on tg0 tables yet`.

**Breaking change in v0.0.1-alpha.22:** earlier versions used `tg0(aux1, aux2, ...)`, where every argument became an untyped auxiliary column next to an implicit `_shape` geometry column. That syntax and its storage layout were removed — newer builds cannot open old tg0 tables, and instead fail with a "tg0 constructor error" pointing at this section. There is no in-place migration: dump the old table with the extension version that created it, for example

```
-- with the OLD extension version loaded:
create table dump as select rowid as id, _shape as shape, aux1, aux2 from old_table;
drop table old_table;
```

or simply re-fetch the source data. Then, with the new version loaded, recreate the table with typed columns and re-insert:

```
create virtual table new_table using tg0(
  id integer primary key,
  shape geometry,
  aux1 text,
  aux2 text
);
insert into new_table(id, shape, aux1, aux2) select id, shape, aux1, aux2 from dump;
```

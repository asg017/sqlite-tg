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
-- 'v0.0.1-alpha.19'
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

```sql
select tg_to_wkt(tg_point(1, 2));
-- 'POINT(1 2)'
```

#### `tg_line(p1, p2, ..., pN)` {#tg_line}

A [pointer function](#pointer-functions) that returns a LineString geometry made up of the given points, in order. Input arguments must be Point geometries, in any [supported format](#supported-formats).

```sql
select tg_to_wkt(tg_line(tg_point(0, 0), tg_point(1, 1), tg_point(2, 0)));
-- 'LINESTRING(0 0,1 1,2 0)'
```

#### `tg_multipoint(p1, p2, ..., pN)` {#tg_multipoint}

A [pointer function](#pointer-functions) that returns a MultiPoint geometry with the given points. Input arguments must be Point geometries, in any [supported format](#supported-formats).

```sql
select tg_to_wkt(tg_multipoint(tg_point(1, 1), tg_point(2, 2)));
-- 'MULTIPOINT(1 1,2 2)'
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

#### `tg_group_geometry_collection(geometry)` {#tg_group_geometry_collection}

A [pointer function](#pointer-functions) and aggregate function that builds a GeometryCollection from the aggregated geometries, which may be of mixed types.

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

#### `tg0(aux1, aux2, ...)` {#tg0}

An experimental virtual table that stores geometries in the `_shape` column, backed by an [R-Tree index](https://www.sqlite.org/rtree.html) on their bounding boxes for accelerated spatial queries. Requires the R-Tree extension to be compiled into the host SQLite. Any arguments become auxiliary columns on the table. Expect breaking changes.

```sql
create virtual table businesses using tg0(name);
insert into businesses(rowid, _shape, name) values
  (1, tg_geom('POINT(-122.4075 37.787994)'), 'in sf'),
  (2, tg_geom('POINT(-73.985130 40.758896)'), 'in nyc');
select rowid, name
from businesses
where tg_intersects(_shape, 'POLYGON((
  -122.51610563264538 37.81424532146113,
  -122.51610563264538 37.69618409220847,
  -122.35290547288255 37.69618409220847,
  -122.35290547288255 37.81424532146113,
  -122.51610563264538 37.81424532146113
))');
/*
┌───────┬─────────┐
│ rowid │ name    │
├───────┼─────────┤
│ 1     │ 'in sf' │
└───────┴─────────┘
*/
```

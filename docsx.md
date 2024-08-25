<h4 name="tg_version"><code>tg_version()</code></h4>

Returns the version string of `sqlite-tg`.


```sql
select tg_version();
```
<h4 name="tg_debug"><code>tg_debug()</code></h4>

Returns fuller debug information of `sqlite-tg`.


```sql
select tg_debug();
```
<h4 name="tg_point"><code>tg_point(x, y)</code></h4>

A [pointer function](#pointer-functions) that returns a point geometry
with the given `x` and `y` values. This value will appear to be `NULL`
on direct access, and is meant for performance critical SQL queries
where you want to avoid serializing/de-serializing.



```sql
select
  tg_point(1, 2) as p1,
  tg_to_wkt(tg_point(1, 2)) as p2;
```
<h4 name="tg_multipoint"><code>tg_multipoint(p1, p2, ...)</code></h4>

A [pointer function](#pointer-functions) that returns a MultiPoint
geometry with the given points. This value will appear to be `NULL` on
direct access, so consider TODO

Input arguments must be Point geometries, which can be WKT, WKB, or GeoJSON.



```sql
select
  tg_multipoint(
    tg_point(0, 0),
    tg_point(1, 1),
    tg_point(2, 2)
  ) as p1,
  tg_to_wkt(
    tg_multipoint(
      tg_point(0, 0),
      tg_point(1, 1),
      tg_point(2, 2)
    )
  ) as p2;
```
<h4 name="tg_group_multipoint"><code>tg_group_multipoint()</code></h4>

An aggregate function that returns a MuliPoint geometry with the given
points.

Input arguments must be Point geometries in WKT, WKB, or GeoJSON format.



```sql
select
  tg_to_wkt(
    tg_group_multipoint(
      tg_point(column1, column2)
    )
  ) as p1
from (
  values (0, 0), (1, 1), (2, 2)
)
```
<h4 name="to_to_geojson"><code>to_to_geojson(geometry)</code></h4>

Converts the given geometry into a GeoJSON string. Inputs can be in
[any supported formats](#supported-formats), including WKT, WKB, and
GeoJSON. Based on [`tg_geom_geojson()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_geojson).



```sql
select
  tg_to_geojson('POINT(0 1)') as src_wkt,
  tg_to_geojson(X'01010000000000000000000000000000000000f03f') as src_wkb,
  tg_to_geojson('{"type":"Point","coordinates":[0,1]}') as src_geojson,
  tg_to_geojson(tg_point(0, 1)) as src_pointer;
```
<h4 name="to_to_wkb"><code>to_to_wkb(geometry)</code></h4>

Converts the given geometry into a WKB blob. Inputs can be in
[any supported formats](#supported-formats), including WKT, WKB, and
GeoJSON. Based on [`tg_geom_wkb()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_wkb).



```sql
select
  tg_to_wkb('POINT(0 1)') as src_wkt,
  tg_to_wkb(X'01010000000000000000000000000000000000f03f') as src_wkb,
  tg_to_wkb('{"type":"Point","coordinates":[0,1]}') as src_geojson,
  tg_to_wkb(tg_point(0, 1)) as src_pointer;
```
<h4 name="tg_to_wkt"><code>tg_to_wkt(geometry)</code></h4>

Converts the given geometry into a WKT blob. Inputs can be in
[any supported formats](#supported-formats), including WKT, WKB, and
GeoJSON. Based on [`tg_geom_wkt()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_wkt).



```sql
select
  tg_to_wkt('POINT(0 1)') as src_wkt,

  tg_to_wkt(X'01010000000000000000000000000000000000f03f') as src_wkb,
  tg_to_wkt('{"type":"Point","coordinates":[0,1]}') as src_geojson,
  tg_to_wkt(tg_point(0, 1)) as src_pointer;
```
<h4 name="tg_type"><code>tg_type(geometry)</code></h4>

Returns a string describing the type of the provided `geometry`. Inputs
can be in [any supported formats](#supported-formats), including WKT,
WKB, and GeoJSON. Based on [`tg_geom_type_string()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_type_string).

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
select
  tg_type('POINT (1 1)') as p,
  tg_type('LINESTRING (1 1, 2 2, 3 3)') as ls,
  tg_type('POLYGON ((3 1, 4 4, 2 4, 1 2, 3 1))') as poly,
  tg_type('MULTIPOINT (1 4, 4 3, 2 2, 3 1)') as mp,
  tg_type('MULTIPOLYGON (((3 2, 4.5 4, 1 4, 3 2)),((15 5, 40 10, 10 20, 5 10, 15 5)))') as mpoly,
  tg_type('GEOMETRYCOLLECTION ( POINT (1 1), POINT (1 1) )') as gc;
```
<h4 name="tg_extra_json"><code>tg_extra_json(geometry)</code></h4>

If the original geometry is a GeoJSON with extra fields such as `id` or
`property`, those extra fields will be returned in a JSON object.



```sql
select
  tg_extra_json('{
    "type": "Point",
    "coordinates": [-118.2097812,34.0437074]
  }') as no_extra,
  tg_extra_json('{
    "id": "ASG0017",
    "type": "Point",
    "coordinates": [-118.2097812,34.0437074],
    "properties": {"color": "red"}
  }') as some_extra;
```
<h4 name="tg_intersects"><code>tg_intersects(a, b)</code></h4>

Returns `1` if the `a` geometry intersects the `b` geometry, otherwise returns `0`.
Will raise an error if either `a` or `b` are not valid geometries.
Based on [`tg_geom_intersects()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_intersects).

The `a` and `b` geometries can be in any [supported format](#supported-formats), including WKT, WKB, and GeoJSON.



```sql
select
  tg_intersects(
    'LINESTRING (0 0, 2 2)',
    'LINESTRING (1 0, 1 2)'
  ) as result1,
  tg_intersects(
    'LINESTRING (0 0, 0 2)',
    'LINESTRING (2 0, 2 2)'
  ) as result2;
```
<h4 name="tg_contains"><code>tg_contains(a, b)</code></h4>

pls



```sql
select
  tg_contains(
    'POLYGON ((0 0, 0 1, 1 1, 1 0, 0 0))',
    'POINT (0.5 0.5)'
  ) as result1,
  tg_contains(
    'POLYGON ((0 0, 0 1, 1 1, 1 0, 0 0))',
    'POINT (0 0)'
  ) as result2;
```

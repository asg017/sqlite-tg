# Introduction

`sqlite-tg` is a SQLite extension for fast geospatial operations — a thin
wrapper around the excellent [tg](https://github.com/tidwall/tg) geometry
library by [Josh Baker](https://github.com/tidwall). It brings point-in-polygon
tests, geometry intersections, and WKT/WKB/GeoJSON parsing and conversions to
any SQLite database, as plain SQL functions.

```sql
select tg_intersects(
  'POLYGON((-122.51 37.81, -122.51 37.69, -122.35 37.69, -122.35 37.81, -122.51 37.81))',
  'POINT(-122.4075 37.787994)'
);
-- 1
```

Because it is a standard [run-time loadable extension](https://www.sqlite.org/loadext.html)
written in C with no other dependencies, it runs anywhere SQLite runs —
laptops, servers, mobile devices, and browsers via WASM. Try it right now in
the [Playground](/playground).

::: warning
`sqlite-tg` is still young and pre-v1, so expect breaking changes.
:::

See the [API Reference](/api-reference) for every function `sqlite-tg`
provides, and [Installation](/installation) for how to install it in your
language or tool of choice.

## Supported formats {#supported-formats}

`tg` and `sqlite-tg` accept geometries in
[Well-known Text (WKT)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry),
[Well-known Binary (WKB)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry#Well-known_binary),
and [GeoJSON](https://geojson.org/) formats.

`sqlite-tg` functions will infer which format to use based on the following
rules:

1. If a provided argument is a `BLOB`, then it is assumed the blob is valid
   WKB.
2. If the provided argument is `TEXT` and is the return value of a
   [JSON SQL function](https://www.sqlite.org/json1.html), or if it starts
   with `"{"`, then it is assumed the string is valid GeoJSON.
3. If the provided argument is still `TEXT`, then it is assumed the text is
   valid WKT.
4. If the provided argument is the return value of a `sqlite-tg` function that
   [returns a geometry pointer](#pointer-functions), the underlying geometry
   is used directly.

Convert between the formats with
[`tg_to_wkt()`](/api-reference#tg_to_wkt),
[`tg_to_wkb()`](/api-reference#tg_to_wkb), and
[`tg_to_geojson()`](/api-reference#tg_to_geojson):

```sql
select tg_to_geojson('POINT(1 2)');
-- '{"type":"Point","coordinates":[1,2]}'

select tg_to_wkb('POINT(1 2)');
-- X'0101000000000000000000f03f0000000000000040'

select tg_to_wkt('{"type":"Point","coordinates":[1,2]}');
-- 'POINT(1 2)'
```

## Pointer functions {#pointer-functions}

Some functions in `sqlite-tg` use SQLite's
[Pointer Passing Interface](https://www.sqlite.org/bindptr.html) to return
special objects. This is mainly done for performance benefits in specific
queries, to avoid the overhead of serializing/de-serializing the same
geometric object multiple times.

When using one of these functions it may appear to return `NULL`. Technically
it is not null, but user-facing SQL queries can't directly access the real
value. Instead, other `sqlite-tg` functions can read the underlying data in
their own functions. For example:

```sql
select tg_point(1, 2);
-- NULL
select tg_to_wkt(tg_point(1, 2));
-- 'POINT(1 2)'
```

`tg_point` is a pointer function, which appears to return `NULL` when directly
accessed in a query. However, it can be passed into other `sqlite-tg`
functions, such as `tg_to_wkt()`, which access the underlying geometric object
and serialize it to WKT.

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

The above query returns an error because the "pointer" returned from
`tg_point()` inside `step1` doesn't exist outside the `step1` CTE boundary.
When accessed in `step2`, the `point1` return is `NULL`, so `tg_to_wkt()`
throws the error.

The solution is to "serialize" the point with `tg_to_wkt` inside of `step1`.
This ensures that `point1` will be a normal SQL `TEXT` value, and can be
queried in other table expressions like normal.

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

# `sqlite-tg` Documentation

As a reminder, `sqlite-tg` is still young, so breaking changes should be expected while `sqlite-tg` is in a pre-v1 stage.

## Supported Formats

`tg` and `sqlite-tg` can accept geometries in [Well-known Text (WKT)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry), [Well-known Binary(WKB)](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry#Well-known_binary), and [GeoJSON](https://geojson.org/) formats.

`sqlite-tg` functions will infer which format to use based on the following rules:

1. If a provided argument is a `BLOB`, then it is assumed the blob is valid WKB.
2. If the provided argument is `TEXT` and is the return value of a [JSON SQL function](https://www.sqlite.org/json1.html), or if it starts with `"{"`, then it is assumed the string is valid GeoJSON.
3. If the provided argument is still text, then it is assumed the text is valid WKT.

## API Reference

All functions offered by `sqlite-tg`.

## Functions

<h3 name="tg_version"><code>tg_version()</code></h3>

Returns the version of `sqlite-tg`.

```sql
select tg_version(); -- "v0...."
```

<h3 name="tg_debug_"><code>tg_debug()</code></h3>

Returns fuller debug information of `sqlite-tg`.

```sql
select tg_debug(); -- "v0....Date...Commit..."
```

<h3 name="tg_intersects"><code>tg_intersects(a, b)</code></h3>

Based on [`tg_geom_intersects()`](https://github.com/tidwall/tg/blob/main/docs/API.md#tg_geom_intersects).

```sql
select tg_intersects(a, b);
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
select tg_intersects('POLYGON((
  -122.51610563264538 37.81424532146113,
  -122.51610563264538 37.69618409220847,
  -122.35290547288255 37.69618409220847,
  -122.35290547288255 37.81424532146113,
  -122.51610563264538 37.81424532146113
))', 'POINT(-122.4075 37.787994)')
```

With a point outside the city it returns `0`:

```sql
select tg_intersects('POLYGON((
  -122.51610563264538 37.81424532146113,
  -122.51610563264538 37.69618409220847,
  -122.35290547288255 37.69618409220847,
  -122.35290547288255 37.81424532146113,
  -122.51610563264538 37.81424532146113
))', 'POINT(-73.985130 40.758896)')
```

<!--
<h3 name="tg_XXX"><code>tg_XXX()</code></h3>

```sql
select tg_XXX();
```
-->

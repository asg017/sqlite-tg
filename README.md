# sqlite-tg

Work-in-progress geospatial SQLite extension around [tg](https://github.com/tidwall/tg). Not ready yet, but feel free to play with it!

Once stabilized, will be a part of [sqlite-ecosystem](https://github.com/asg017/sqlite-ecosystem).

## Functions

This extension adds the following SQLite functions:

### tg_intersects(wkt_string, wkt_string)

Checks if the geospatial objects represented by the provided [WKT strings](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry) intersect each other.

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

### tg_version()

Returns the version of this extension:

```sql
select tg_version()
```
```
v0.0.1-alpha.4
```

### tg_debug()

A more extensive set of version information:

```sql
select tg_debug()
```
```
Version: v0.0.1-alpha.4
Date: 2023-09-23T06:36:03Z+0000
Source: 13410670cfeb0b5ad03a5ec9edf559acdaada49d
```

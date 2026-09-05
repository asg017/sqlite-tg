# tg0 KNN — release notes snippet

## New: nearest-neighbor queries on `tg0` tables

`geomcol MATCH <point>` puts a `tg0` scan into nearest-neighbor mode. Rows
stream out nearest-first and the hidden `distance` column carries each
row's distance, so the usual shapes just work:

```sql
-- the 10 places nearest a lon/lat
select id, name, distance
from places
where geom match tg_point(:lng, :lat)
order by distance
limit 10;

-- everything within 5 km, nearest first
select id, name, distance
from places
where geom match tg_point(:lng, :lat) and distance <= 5000
order by distance;

-- the nearest places inside a bounding box
select id, name, distance
from places
where geom match tg_point(:lng, :lat)
  and tg_intersects(geom, :bbox_polygon)
order by distance
limit 10;

-- the nearest place for every row of another table
select q.name, (
  select p.name from places p
  where p.geom match q.location
  order by p.distance limit 1
) as nearest
from queries q;
```

The query point can be any Point value sqlite-tg accepts (`tg_point()`,
WKT, WKB, GeoJSON). Candidates can be any geometry type: the distance is to
the candidate's nearest point, and a polygon containing the query point is
at distance `0`. Always write `ORDER BY distance` with a `LIMIT`; that
order is served straight from the index, anything else makes SQLite sort
the full stream.

- **`metric=geodesic|planar`** is a new per-geometry-column option. The
  default, `geodesic`, treats coordinates as longitude/latitude and reports
  meters (great-circle distance on the IUGG mean-radius sphere, within
  ~0.5% of the WGS84 ellipsoid); `planar` reports Euclidean distance in
  coordinate units for projected or abstract coordinates.
- Same-column predicates (`tg_intersects(geom, ...)` etc.) and
  `distance < ?` / `distance <= ?` are pushed into the index walk, so
  bbox-restricted and radius queries touch only the relevant part of the
  tree. Other WHERE terms are filtered on the stream and stay correct.
- New scalar **`tg_distance_sphere(a, b)`**: great-circle meters between a
  Point and the nearest point of any geometry.

## ⚠️ Breaking change: geodesic columns validate coordinates

Geometry columns without `metric=planar` now reject coordinates outside the
longitude/latitude range (and NaN/infinite coordinates) on INSERT and
UPDATE with a `SQLITE_CONSTRAINT` error naming the option. Tables holding
projected coordinates must be declared with `metric=planar`.

## Limitations

- The MATCH argument must be a Point; there is no geometry-to-geometry
  distance.
- Line and polygon edges are straight lines in longitude/latitude (as in
  every other `tg_*` function); geometries crossing the antimeridian should
  be cut at it. Distances are spherical, not ellipsoidal.

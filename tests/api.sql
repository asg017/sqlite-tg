.load dist/tg0

-- [x] Coverage (functions, vtabs)
-- [ ] authorizater deny
-- [ ] shadow table contents?
-- [ ] fuzzer?


-- #region meta

select regex_replace('(v)(.*)', tg_version(), '${1}REDACTED'); -- 'vREDACTED'



select
  regex_replace(
    '(?<key>[^:]:)(.*)',
    line,
    '$key REDACTED'
  ) as line_redacted
from str_lines(tg_debug()); -- @snap debug

-- #endregion

-- #region tg_point
  select tg_point(1, 2); -- @snap point
  select subtype(tg_point(1, 2)); -- 112
  select tg_to_wkt(tg_point(1, 3)); -- 'POINT(1 3)'
  select tg_to_wkb(tg_point(1, 2)); -- X'0101000000000000000000f03f0000000000000040'
  select tg_to_geojson(tg_point(1, 2)); -- '{"type":"Point","coordinates":[1,2]}'
  -- errors
    select tg_point('a', 1); -- error: point X value must be an integer or float
    select tg_point(1, 'a'); -- error: point Y value must be an integer or float
-- #endregion


-- #region tg_points_each
  select
    rowid,
    point,
    tg_to_wkb(point),
    tg_to_wkt(point),
    tg_to_geojson(point)
  from tg_points_each('MULTIPOINT ((10 40), (40 30), (20 20), (30 10))'); -- @snap points-each

  select
    json_each.key,
    tg_points_each.rowid,
    point,
    tg_to_wkt(point),
    tg_to_geojson(point)
  from json_each('
    [
      "MULTIPOINT ((10 20), (30 40))",
      "MULTIPOINT (90 80,70 60,50 40)",
    ]
  ')
  join tg_points_each(value); -- @snap points-each-join
-- #endregion

-- #region tg_lines_each
  select
    rowid,
    line,
    tg_to_wkb(line),
    tg_to_wkt(line),
    tg_to_geojson(line)
  from tg_lines_each('MULTILINESTRING ((10 40, 40 30), (20 20, 30 10))'); -- @snap lines-each

  select
    json_each.key,
    tg_lines_each.rowid,
    line,
    tg_to_wkb(line),
    tg_to_wkt(line),
    tg_to_geojson(line)
  from json_each('[
    "MULTILINESTRING ((10 40, 40 30), (20 20, 30 10))",
    "MULTILINESTRING ((50 60, 70 80))",
  ]')
  join tg_lines_each(value); -- @snap lines-each-join

  select * from tg_lines_each(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_polygons_each
  select
    rowid,
    polygon,
    tg_to_wkb(polygon),
    tg_to_wkt(polygon),
    tg_to_geojson(polygon)
  from tg_polygons_each('MULTIPOLYGON (((10 40, 40 30, 20 20, 10 40)), ((30 20, 20 10, 10 30, 30 20)))'); -- @snap polygons-each

  select
    json_each.key,
    tg_polygons_each.rowid,
    polygon,
    tg_to_wkb(polygon),
    tg_to_wkt(polygon),
    tg_to_geojson(polygon)
  from json_each('[
    "MULTIPOLYGON (((10 40, 40 30, 20 20, 10 40)), ((30 20, 20 10, 10 30, 30 20)))",
    "MULTIPOLYGON (((50 60, 70 80, 90 100, 50 60)))",
    "POLYGON EMPTY"
   ]')
   join tg_polygons_each(value); -- @snap polygons-each-join
-- #endregion

-- #region tg_geometries_each
  select
    rowid,
    geometry,
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from tg_geometries_each('GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))'); -- @snap geometries-each

  select
    json_each.key,
    tg_geometries_each.rowid,
    geometry,
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from json_each('[
    "GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))",
    "GEOMETRYCOLLECTION (POLYGON ((10 20, 30 40, 50 60, 10 20)))",
   ]')
   join tg_geometries_each(value); -- @snap geometries-each-join
-- #endregion

-- #region tg_multipoint
  select tg_to_wkt(
    tg_multipoint(tg_point(1, 2))
  ); -- 'MULTIPOINT(1 2)'
  select tg_to_wkt(
    tg_multipoint(
      tg_point(1, 2),
      tg_point(3, 4)
    )
  ); -- 'MULTIPOINT(1 2,3 4)'
  select tg_to_wkt(
    tg_multipoint(
      tg_point(1, 2),
      tg_point(3, 4),
      tg_point(5, 6),
      tg_point(7, 8)
    )
  ); -- 'MULTIPOINT(1 2,3 4,5 6,7 8)'

  -- errors
  select tg_multipoint(NULL); -- error: argument to tg_multipoint() at index 0 is an invalid geometry: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
  --select tg_multipoint(tg_line);
-- #endregion

-- #region tg_extra_json
  select tg_extra_json('{"type": "Point","coordinates": [-118.2097812,34.0437074]}'); -- NULL

  select tg_extra_json('{"id": "A", "type": "Point","coordinates": [-118.2097812,34.0437074]}'); -- '{"id":"A"}'
-- #endregion


-- #region tg_line
  select tg_line(); -- @snap line-zero-args
  select tg_to_wkt(tg_line()); -- 'LINESTRING EMPTY'
  select tg_to_wkt(
    tg_line(
      tg_point(1, 2),
      tg_point(3, 4)
    )
  ); -- 'LINESTRING(1 2,3 4)'
  select tg_line(tg_point(1, 1), 'not point'); -- error: argument to tg_line() at index 1 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_line('LINESTRING EMPTY'); -- error: argument to tg_line() at index 0 expected a point, found LineString
  --select t
-- #endregion

-- #region tg_type
  select
    tg_type(value)
  from json_each('[
    "POINT EMPTY",
    "LINESTRING EMPTY",
    "POLYGON EMPTY",
    "MULTIPOINT EMPTY",
    "MULTIPOLYGON EMPTY",
    "MULTILINESTRING EMPTY",
    "GEOMETRYCOLLECTION EMPTY",
  ]'); -- @snap type-all

  select tg_type(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_poly_exterior
  select tg_to_wkt(tg_poly_exterior('POLYGON ((1 2, 3 4, 5 6, 1 2))')); -- 'POLYGON((1 2,3 4,5 6,1 2))'
  select tg_to_wkt(tg_poly_exterior('POLYGON ((1 2, 3 4, 5 6, 1 2), (7 8, 9 10, 11 12, 7 8))')); -- 'POLYGON((1 2,3 4,5 6,1 2))'
  --select tg_poly_exterior('POLYGON EMPTY');
-- #endregion

-- #region tg_holes_each
  select
    rowid,
    *,
    tg_to_wkt(hole)
  from tg_holes_each('POLYGON ((1 2, 3 4, 5 6, 1 2), (7 8, 9 10, 11 12, 7 8))'); -- @snap holes-each

  -- empty
  select
    rowid,
    *,
    tg_to_wkt(hole)
  from tg_holes_each('POLYGON ((1 2, 3 4, 5 6, 1 2))'); -- [no results]

  select
    tg_holes_each.rowid as "tg_holes_each.rowid",
    json_each.rowid as "json_each.rowid",
    json_each.key,
    tg_to_wkt(hole)
  from json_each('[
    "POLYGON ((1 1, 2 2, 1 1), (3 3, 4 4, 3 3))",
    "POLYGON ((1 1, 2 2, 1 1), (5 5, 6 6, 5 5), (7 7, 8 8, 7 7))",
  ]')
  join tg_holes_each(json_each.value); -- @snap holes-each-join

  -- non-polygon inputs yield zero rows instead of misreading memory
  select count(*) from tg_holes_each('POINT(1 1)'); -- 0
  select count(*) from tg_holes_each('LINESTRING(0 0, 1 1)'); -- 0
  select count(*) from tg_holes_each('MULTIPOINT(1 1, 2 2)'); -- 0

  -- GeoJSON polygons with extra fields parse to a wrapper geom; holes must
  -- still be found through it
  select tg_to_wkt(hole)
  from tg_holes_each('{
    "type": "Polygon",
    "coordinates": [[[0,0],[10,0],[10,10],[0,0]],[[1,1],[2,1],[2,2],[1,1]]],
    "extra": 1
  }'); -- 'POLYGON((1 1,2 1,2 2,1 1))'
-- #endregion

-- #region tg_each
  select
    rowid,
    geometry,
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from tg_each('GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))'); -- @snap each
  select
    json_each.key,
    tg_each.rowid,
    geometry,
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from json_each('[
    "GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))",
    "GEOMETRYCOLLECTION (POLYGON ((10 20, 30 40, 50 60, 10 20)))",
   ]')
   join tg_each(value); -- @snap each-join
-- #endregion

-- #region tg_valid_geojson
  select tg_valid_geojson('{"type":"Point","coordinates":[0.5,0.5]}'); -- 1
  select tg_valid_geojson('POINT(0.5 0.5)'); -- 0
  select tg_valid_geojson(X'0101000000000000000000E03F000000000000E03F'); -- 0
-- #endregion

-- #region tg_valid_wkt
  select tg_valid_wkt('{"type":"Point","coordinates":[0.5,0.5]}'); -- 0
  select tg_valid_wkt('POINT(0.5 0.5)'); -- 1
  select tg_valid_wkt(X'0101000000000000000000E03F000000000000E03F'); -- 0
 -- #endregion

-- #region tg_valid_wkb
  select tg_valid_wkb('{"type":"Point","coordinates":[0.5,0.5]}'); -- 0
  select tg_valid_wkb('POINT(0.5 0.5)'); -- 0
  select tg_valid_wkb(X'0101000000000000000000E03F000000000000E03F'); -- 1
-- #endregion

-- #region tg_group_multipoint
  select
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[
    "POINT(1 1)",
    "POINT(2 2)",
    "POINT(3 3)",
  ]'); -- 'MULTIPOINT(1 1,2 2,3 3)'

  select
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[]'); -- 'MULTIPOINT EMPTY'

  select
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[
    "POINT (1 1)",
    "LINESTRING (2 2, 3 3)",
  ]'); -- error: parameters to tg_group_multipoint() must be Point gemetries
-- #endregion

-- #region tg_group_multipolygon
select
  tg_to_wkt(tg_group_multipolygon(value))
from json_each('[
  "POLYGON ((1 1, 2 2, 3 3, 1 1))",
  "POLYGON ((4 4, 5 5, 6 6, 4 4))",
  "POLYGON ((7 7, 8 8, 9 9, 7 7))",
]'); -- 'MULTIPOLYGON(((1 1,2 2,3 3,1 1)),((4 4,5 5,6 6,4 4)),((7 7,8 8,9 9,7 7)))'

select
  tg_to_wkt(tg_group_multipolygon(value))
from json_each('[]'); -- 'MULTIPOLYGON EMPTY'

select
  tg_to_wkt(tg_group_multipolygon(value))
from json_each('[
  "POLYGON ((1 1, 2 2, 3 3, 1 1))",
  "POINT (3 3)",
]'); -- error: inputs to tg_group_multipolygon() must be Polygon geometries
-- #endregion

-- #region tg_group_multilinestring
select
  tg_to_wkt(
    tg_group_multilinestring(value)
  )
from json_each('[
  "LINESTRING (1 1, 2 2)",
  "LINESTRING (3 3, 4 4)",
  "LINESTRING (5 5, 6 6)",
]'); -- 'MULTILINESTRING((1 1,2 2),(3 3,4 4),(5 5,6 6))'

select
  tg_to_wkt(
    tg_group_multilinestring(value)
  )
from json_each('[]'); -- 'MULTILINESTRING EMPTY'

select tg_group_multilinestring(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
select tg_group_multilinestring('POINT (1 1)'); -- error: inputs to tg_group_multilinestring() must be LineString geometries
-- #endregion

-- #region tg_group_geometry_collection
select
  tg_to_wkt(
    tg_group_geometry_collection(value)
  )
from json_each('[
  "POINT (1 1)",
  "LINESTRING (2 2, 3 3)",
  "POLYGON ((4 4, 5 5, 6 6, 4 4))",
]'); -- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(2 2,3 3),POLYGON((4 4,5 5,6 6,4 4)))'

select
  tg_to_wkt(
    tg_group_geometry_collection(value)
  )
from json_each('[]'); -- 'GEOMETRYCOLLECTION EMPTY'

select tg_group_geometry_collection(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_group_feature_collection_geojson
select (
  json_pretty(
    tg_group_feature_collection_geojson(
      tg_point(value ->> 1, value ->> 2),
      json_object(
        'name', value ->> 0,
        'population', value ->> 3
      )
    )
  )
) as geo
from json_each('[
  ["New York", 40.7128, -74.0060, 8419600],
  ["Los Angeles", 34.0522, -118.2437, 3980400],
  ["Chicago", 41.8781, -87.6298, 2716000]
]'); -- @snap group-feature-collection-geojson
-- #endregion

-- #region tg_group_bbox
select
  tg_to_wkt(tg_group_bbox(value))
from json_each('[
  "POINT (1 1)",
  "LINESTRING (2 2, 3 3)"
]'); -- 'POLYGON((1 1,3 1,3 3,1 3,1 1))'
-- #endregion

-- #region tg_bbox
select * from tg_bbox('POINT (1 1)'); -- @snap bbox-point
select * from tg_bbox('POLYGON ((1 1, 2 2, 3 3, 1 1))'); -- @snap bbox-polygon
select * from tg_bbox(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
select
  value,
  tg_bbox.*
from json_each('[
  "POINT (1 1)",
  "POLYGON ((1 1, 2 2, 3 3, 1 1))",
  "MULTIPOLYGON (((1 1, 2 2, 3 3, 1 1)), ((4 4, 5 5, 6 6, 4 4)))",
  "LINESTRING (4 4, 6 6, 7 7)",
]')
join tg_bbox(value); -- @snap bbox-join
-- #endregion

-- #region predicates
-- TODO more predicate testing
create table predicate_test_cases as
  select
    value ->> 0 as a,
    value ->> 1 as b
  from json_each('[
    ["POINT (1 1)", "POINT (1 1)"],
  ]');

select
  a,
  b,
  tg_contains(a, b),
  tg_contains(b, a),
  tg_coveredby(a, b),
  tg_coveredby(b, a),
  tg_covers(a, b),
  tg_covers(b, a),
  tg_disjoint(a, b),
  tg_disjoint(b, a),
  tg_intersects(a, b),
  tg_intersects(b, a),
  tg_touches(a, b),
  tg_touches(b, a),
  tg_within(a, b),
  tg_within(b, a)
from predicate_test_cases; -- @snap predicates
-- #endregion

-- #region tg_geom
select tg_geom('POINT (1 1)'); -- @snap geom
select tg_geom('POINT (1 1)', 'none'); -- @snap geom-none
select tg_geom('POINT (1 1)', 'natural'); -- @snap geom-natural
select tg_geom('POINT (1 1)', 'ystripes'); -- @snap geom-ystripes
select tg_geom('POINT (1 1)', 'unknown'); -- error: unrecognized index option. Should be one of none/natural/ystripes
-- #endregion


-- #region tg0
create virtual table tg_demo1 using tg0();
select name from sqlite_master where name like 'tg%' order by 1; -- @snap tg0-schema
insert into tg_demo1(rowid, _shape) select key, value from json_each('[
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818620800527,32.881627962039275],[-117.23803891594858,32.881627962039275],[-117.23803891594858,32.88150426716983],[-117.23818620800527,32.88150426716983],[-117.23818620800527,32.881627962039275]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23794407791227,32.88163023398593],[-117.23779678585558,32.88163023398593],[-117.23779678585558,32.88150653911649],[-117.23794407791227,32.88150653911649],[-117.23794407791227,32.88163023398593]]]},"properties":{},},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818630582224,32.881454314415976],[-117.23803901376554,32.881454314415976],[-117.23803901376554,32.88133061954653],[-117.23818630582224,32.88133061954653],[-117.23818630582224,32.881454314415976]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.2379432340281,32.881454247902326],[-117.2377959419714,32.881454247902326],[-117.2377959419714,32.88133055303288],[-117.2379432340281,32.88133055303288],[-117.2379432340281,32.881454247902326]]]},"properties":{}},
]');
select count(*) from tg_demo1_rtree; -- 4
select rowid, typeof(_shape), tg_to_wkt(_shape) from tg_demo1; -- @snap tg0-rows
select id, minX, maxX, minY, maxY from tg_demo1_rtree; -- @snap tg0-rtree
explain query plan select * from tg_demo1; -- @snap tg0-eqp-scan
explain query plan select * from tg_demo1 where tg_intersects(_shape, ''); -- @snap tg0-eqp-intersects
explain query plan select * from tg_demo1 where tg_contains(_shape, ''); -- @snap tg0-eqp-contains
select rowid, * from tg_demo1; -- @snap tg0-rows-star
-- #endregion

-- #region MISC
create table t as
  select
    tg_extra_json(geometry) as m,
    tg_to_wkb(geometry) as geometry
  from tg_each(readfile('examples/us-states.geojson'));

select octet_length(m), octet_length(geometry) from t; -- @snap misc-octet-lengths
-- #endregion

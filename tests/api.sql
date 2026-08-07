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
  -- Z/M arities
  select tg_to_wkt(tg_point(1, 2, 3)); -- 'POINT(1 2 3)'
  select tg_to_wkt(tg_point(1, 2, 3, 4)); -- 'POINT(1 2 3 4)'
  -- errors
    select tg_point('a', 1); -- error: point X value must be an integer or float
    select tg_point(1, 'a'); -- error: point Y value must be an integer or float
    select tg_point(1, 2, 'a'); -- error: point Z value must be an integer or float
    select tg_point(1, 2, 3, 'a'); -- error: point M value must be an integer or float
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

-- #region tg_line_points_each
  select
    rowid,
    point,
    tg_to_wkt(point),
    tg_to_geojson(point)
  from tg_line_points_each('LINESTRING (10 40, 40 30, 20 20, 30 10)'); -- @snap line-points-each

  select
    json_each.key,
    tg_line_points_each.rowid,
    point,
    tg_to_wkt(point)
  from json_each('[
    "LINESTRING (10 40, 40 30)",
    "LINESTRING (20 20, 30 10, 15 15)",
  ]')
  join tg_line_points_each(value); -- @snap line-points-each-join

  select count(*) from tg_line_points_each('LINESTRING EMPTY'); -- 0
  select * from tg_line_points_each(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
  select * from tg_line_points_each('POINT(1 1)'); -- error: expected a LineString geometry, found Point
  select * from tg_line_points_each('MULTILINESTRING ((10 40, 40 30))'); -- error: expected a LineString geometry, found MultiLineString
-- #endregion

-- #region tg_line_segments_each
  select
    rowid,
    ax, ay, bx, by
  from tg_line_segments_each('LINESTRING (10 40, 40 30, 20 20, 30 10)'); -- @snap line-segments-each

  select
    json_each.key,
    tg_line_segments_each.rowid,
    ax, ay, bx, by
  from json_each('[
    "LINESTRING (10 40, 40 30)",
    "LINESTRING (20 20, 30 10, 15 15)",
  ]')
  join tg_line_segments_each(value); -- @snap line-segments-each-join

  select count(*) from tg_line_segments_each('LINESTRING EMPTY'); -- 0
  select * from tg_line_segments_each('POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))'); -- error: expected a LineString geometry, found Polygon
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

-- #region tg_linestring
  -- alias of tg_line(), same implementation (error messages still say tg_line())
  select tg_to_wkt(tg_linestring()); -- 'LINESTRING EMPTY'
  select tg_to_wkt(
    tg_linestring(
      tg_point(1, 2),
      tg_point(3, 4)
    )
  ); -- 'LINESTRING(1 2,3 4)'
  select tg_linestring('LINESTRING EMPTY'); -- error: argument to tg_line() at index 0 expected a point, found LineString
-- #endregion

-- #region tg_ring
  -- tg_ring_new() does not synthesize a closing point: WKT echoes back
  -- exactly the points given (see comment above tg_ring() in sqlite-tg.c),
  -- even though the ring is geometrically closed either way.
  select tg_to_wkt(tg_ring()); -- 'POLYGON(())'
  select tg_to_wkt(
    tg_ring(
      tg_point(0, 0),
      tg_point(10, 0),
      tg_point(10, 10)
    )
  ); -- 'POLYGON((0 0,10 0,10 10))'
  select tg_to_wkt(
    tg_ring(
      tg_point(0, 0),
      tg_point(10, 0),
      tg_point(10, 10),
      tg_point(0, 0)
    )
  ); -- 'POLYGON((0 0,10 0,10 10,0 0))'
  select tg_ring(tg_point(1, 1), 'not point'); -- error: argument to tg_ring() at index 1 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_ring('LINESTRING EMPTY'); -- error: argument to tg_ring() at index 0 expected a point, found LineString
-- #endregion

-- #region tg_polygon
  select tg_to_wkt(tg_polygon()); -- 'POLYGON EMPTY'
  select tg_to_wkt(
    tg_polygon(
      tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 10), tg_point(0, 0))
    )
  ); -- 'POLYGON((0 0,10 0,10 10,0 10,0 0))'
  select tg_to_wkt(
    tg_polygon(
      tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 10), tg_point(0, 0)),
      tg_ring(tg_point(1, 1), tg_point(2, 1), tg_point(2, 2), tg_point(1, 2), tg_point(1, 1))
    )
  ); -- 'POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))'
  -- a Polygon-typed arg with no holes is accepted the same as a bare ring
  select tg_to_wkt(
    tg_polygon('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))')
  ); -- 'POLYGON((0 0,10 0,10 10,0 10,0 0))'

  select tg_polygon('not a geometry'); -- error: argument to tg_polygon() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_polygon(tg_ring(tg_point(0, 0), tg_point(1, 0), tg_point(1, 1), tg_point(0, 0)), 'POINT(1 1)'); -- error: argument to tg_polygon() at index 1 expected a ring, found Point
  -- a Polygon WITH holes cannot be used as a ring arg: silently using its
  -- exterior would drop the holes' data
  select tg_polygon('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))'); -- error: argument to tg_polygon() at index 0 expected a ring, found a Polygon with holes (its holes would be silently dropped)
-- #endregion

-- #region tg_rect
  -- tg_rect isn't a tg_geom in tg's model, so it's represented at the SQL
  -- boundary as the equivalent closed-ring Polygon (counter-clockwise
  -- winding, closing point repeated).
  select tg_to_wkt(tg_rect(0, 0, 1, 1)); -- 'POLYGON((0 0,1 0,1 1,0 1,0 0))'
  select tg_to_wkt(tg_rect(tg_point(0, 0), tg_point(1, 1))); -- 'POLYGON((0 0,1 0,1 1,0 1,0 0))'
  select tg_to_wkt(tg_rect('POINT(0 0)', 'POINT(1 1)')); -- 'POLYGON((0 0,1 0,1 1,0 1,0 0))'

  -- no normalization: minX > maxX / minY > maxY is passed through as given
  select tg_to_wkt(tg_rect(1, 1, 0, 0)); -- 'POLYGON((1 1,0 1,0 0,1 0,1 1))'

  -- composes with predicates like any other geometry
  select tg_intersects(tg_rect(0, 0, 10, 10), tg_point(5, 5)); -- 1

  select tg_rect('a', 0, 1, 1); -- error: rect minX value must be an integer or float
  select tg_rect(0, 0, 1, 'a'); -- error: rect maxY value must be an integer or float
  select tg_rect('not a geometry', tg_point(1, 1)); -- error: argument to tg_rect() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_rect(tg_point(0, 0), 'LINESTRING(0 0, 1 1)'); -- error: argument to tg_rect() at index 1 expected a point, found LineString
-- #endregion

-- #region tg_segment
  -- tg_segment isn't a tg_geom in tg's model, so it's represented at the SQL
  -- boundary as the equivalent 2-point LineString.
  select tg_to_wkt(tg_segment(0, 0, 1, 1)); -- 'LINESTRING(0 0,1 1)'
  select tg_to_wkt(tg_segment(tg_point(0, 0), tg_point(1, 1))); -- 'LINESTRING(0 0,1 1)'
  select tg_to_wkt(tg_segment('POINT(0 0)', 'POINT(1 1)')); -- 'LINESTRING(0 0,1 1)'

  -- composes with predicates like any other geometry
  select tg_intersects(tg_segment(0, 0, 2, 2), tg_segment(0, 2, 2, 0)); -- 1

  select tg_segment('a', 0, 1, 1); -- error: segment aX value must be an integer or float
  select tg_segment(0, 0, 1, 'a'); -- error: segment bY value must be an integer or float
  select tg_segment('not a geometry', tg_point(1, 1)); -- error: argument to tg_segment() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_segment(tg_point(0, 0), 'LINESTRING(0 0, 1 1)'); -- error: argument to tg_segment() at index 1 expected a point, found LineString
-- #endregion

-- #region tg_multilinestring
  select tg_to_wkt(tg_multilinestring()); -- 'MULTILINESTRING EMPTY'
  select tg_to_wkt(
    tg_multilinestring(
      tg_line(tg_point(0, 0), tg_point(1, 1)),
      tg_line(tg_point(2, 2), tg_point(3, 3))
    )
  ); -- 'MULTILINESTRING((0 0,1 1),(2 2,3 3))'
  select tg_multilinestring('not a geometry'); -- error: argument to tg_multilinestring() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_multilinestring('LINESTRING(0 0, 1 1)', 'POINT(1 1)'); -- error: argument to tg_multilinestring() at index 1 expected a LineString, found Point
-- #endregion

-- #region tg_multipolygon
  select tg_to_wkt(tg_multipolygon()); -- 'MULTIPOLYGON EMPTY'
  select tg_to_wkt(
    tg_multipolygon(
      'POLYGON((0 0, 1 0, 1 1, 0 0))',
      'POLYGON((10 10, 11 10, 11 11, 10 10))'
    )
  ); -- 'MULTIPOLYGON(((0 0,1 0,1 1,0 0)),((10 10,11 10,11 11,10 10)))'
  -- a bare ring upcast pointer is accepted too (reports typeof Polygon)
  select tg_to_wkt(
    tg_multipolygon(tg_ring(tg_point(0, 0), tg_point(1, 0), tg_point(1, 1), tg_point(0, 0)))
  ); -- 'MULTIPOLYGON(((0 0,1 0,1 1,0 0)))'
  select tg_multipolygon('not a geometry'); -- error: argument to tg_multipolygon() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_multipolygon('POLYGON((0 0, 1 0, 1 1, 0 0))', 'POINT(1 1)'); -- error: argument to tg_multipolygon() at index 1 expected a Polygon, found Point
-- #endregion

-- #region tg_geometrycollection
  select tg_to_wkt(tg_geometrycollection()); -- 'GEOMETRYCOLLECTION EMPTY'
  select tg_to_wkt(
    tg_geometrycollection('POINT(1 1)', 'LINESTRING(0 0, 1 1)')
  ); -- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(0 0,1 1))'
  -- mixed types: a polygon with a hole, and a nested collection
  select tg_to_wkt(
    tg_geometrycollection(
      'POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))',
      'GEOMETRYCOLLECTION(POINT(2 2))'
    )
  ); -- 'GEOMETRYCOLLECTION(POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1)),GEOMETRYCOLLECTION(POINT(2 2)))'
  select tg_geometrycollection('not a geometry'); -- error: argument to tg_geometrycollection() at index 0 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
  select tg_geometrycollection('POINT(1 1)', 'not a geometry'); -- error: argument to tg_geometrycollection() at index 1 is an invalid geometry: ParseError: invalid type specifier, expected 'Z', 'M', 'ZM', or 'EMPTY'
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

-- #region tg_dims
  select tg_dims('POINT (0 1)'); -- 2
  select tg_dims(tg_point(1, 2)); -- 2
  select tg_dims(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_is_empty
  select tg_is_empty('POINT EMPTY'); -- 1
  select tg_is_empty('POINT (0 1)'); -- 0
  select tg_is_empty(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_is_feature
  select tg_is_feature('{"type":"Feature","geometry":{"type":"Point","coordinates":[0,1]},"properties":{}}'); -- 1
  select tg_is_feature('{"type":"Point","coordinates":[0,1]}'); -- 0
  select tg_is_feature('{"type":"FeatureCollection","features":[]}'); -- 0
  select tg_is_feature(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_is_featurecollection
  select tg_is_featurecollection('{"type":"FeatureCollection","features":[]}'); -- 1
  select tg_is_featurecollection('{"type":"Point","coordinates":[0,1]}'); -- 0
  select tg_is_featurecollection('{"type":"Feature","geometry":{"type":"Point","coordinates":[0,1]},"properties":{}}'); -- 0
  select tg_is_featurecollection(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_x
  select tg_x('POINT (1 2)'); -- 1.0
  select tg_x(tg_point(1, 2)); -- 1.0
  select tg_x('LINESTRING (0 0, 1 1)'); -- error: tg_x() expected a Point geometry, found LineString
  select tg_x(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_y
  select tg_y('POINT (1 2)'); -- 2.0
  select tg_y(tg_point(1, 2)); -- 2.0
  select tg_y('LINESTRING (0 0, 1 1)'); -- error: tg_y() expected a Point geometry, found LineString
  select tg_y(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_has_z
  select tg_has_z('POINT (1 2)'); -- 0
  select tg_has_z('POINT Z (1 2 3)'); -- 1
  select tg_has_z(tg_point(1, 2)); -- 0
  select tg_has_z(tg_point(1, 2, 3)); -- 1
  select tg_has_z('LINESTRING Z (0 0 1, 1 1 1)'); -- 1
  select tg_has_z(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_has_m
  select tg_has_m('POINT (1 2)'); -- 0
  select tg_has_m('POINT ZM (1 2 3 4)'); -- 1
  select tg_has_m(tg_point(1, 2, 3)); -- 0
  select tg_has_m(tg_point(1, 2, 3, 4)); -- 1
  select tg_has_m('LINESTRING ZM (0 0 1 1, 1 1 1 1)'); -- 1
  select tg_has_m(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_z
  -- 2D point: no Z coordinate, returns NULL (not 0, which would be
  -- ambiguous with a real Z value of 0)
  select tg_z('POINT (1 2)'); -- NULL
  select tg_z('POINT Z (1 2 3)'); -- 3.0
  select tg_z(tg_point(1, 2, 3)); -- 3.0
  select tg_z(tg_point(1, 2, 3, 4)); -- 3.0
  select tg_z('LINESTRING (0 0, 1 1)'); -- error: tg_z() expected a Point geometry, found LineString
  select tg_z(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_m
  -- same NULL-for-absent convention as tg_z()
  select tg_m('POINT (1 2)'); -- NULL
  select tg_m('POINT Z (1 2 3)'); -- NULL
  select tg_m('POINT ZM (1 2 3 4)'); -- 4.0
  select tg_m(tg_point(1, 2, 3, 4)); -- 4.0
  select tg_m('LINESTRING (0 0, 1 1)'); -- error: tg_m() expected a Point geometry, found LineString
  select tg_m(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_min_x
  select tg_min_x('POLYGON ((1 1, 2 2, 3 3, 1 1))'); -- 1.0
  select tg_min_x(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_min_y
  select tg_min_y('POLYGON ((1 1, 2 2, 3 3, 1 1))'); -- 1.0
  select tg_min_y(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_max_x
  select tg_max_x('POLYGON ((1 1, 2 2, 3 3, 1 1))'); -- 3.0
  select tg_max_x(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_max_y
  select tg_max_y('POLYGON ((1 1, 2 2, 3 3, 1 1))'); -- 3.0
  select tg_max_y(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
-- #endregion

-- #region tg_memsize
  select tg_memsize('POINT (0 1)'); -- 24
  select tg_memsize(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
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

-- #region tg_ring_points_each
  select
    rowid,
    point,
    tg_to_wkt(point)
  from tg_ring_points_each(
    tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))')
  ); -- @snap ring-points-each

  -- a plain Polygon is also accepted: its exterior ring is used automatically,
  -- same rows as above
  select
    rowid,
    tg_to_wkt(point)
  from tg_ring_points_each('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))'); -- @snap ring-points-each-polygon

  -- a hole ring (also an upcast ring pointer, same as tg_poly_exterior()) works too
  select
    tg_ring_points_each.rowid,
    tg_to_wkt(point)
  from tg_holes_each('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))')
  join tg_ring_points_each(hole); -- @snap ring-points-each-hole-join

  select * from tg_ring_points_each(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
  select * from tg_ring_points_each('POINT(1 1)'); -- error: expected a ring (e.g. from tg_poly_exterior() or tg_holes_each()) or a Polygon geometry, found Point
-- #endregion

-- #region tg_ring_segments_each
  select
    rowid,
    ax, ay, bx, by
  from tg_ring_segments_each(
    tg_poly_exterior('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))')
  ); -- @snap ring-segments-each

  select
    rowid,
    ax, ay, bx, by
  from tg_ring_segments_each('POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))'); -- @snap ring-segments-each-polygon

  select * from tg_ring_segments_each('MULTIPOINT (1 1, 2 2)'); -- error: expected a ring (e.g. from tg_poly_exterior() or tg_holes_each()) or a Polygon geometry, found MultiPoint
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

-- #region tg_group_line
-- aggregate input order is only defined with an explicit `order by` inside
-- the aggregate call, or over an already-ordered subquery
select
  tg_to_wkt(tg_group_line(tg_point(value ->> 0, value ->> 1)))
from json_each('[[1, 1], [2, 2]]'); -- 'LINESTRING(1 1,2 2)'

select
  tg_to_wkt(tg_group_line(tg_point(1, 1)))
from (select 1 where 0); -- 'LINESTRING EMPTY'

select tg_group_line(value) from json_each('[
  "LINESTRING(0 0, 1 1)",
]'); -- error: parameters to tg_group_line() must be Point geometries
-- #endregion

-- #region tg_group_ring
select
  tg_to_wkt(tg_group_ring(tg_point(value ->> 0, value ->> 1)))
from json_each('[[0, 0], [10, 0], [10, 10]]'); -- 'POLYGON((0 0,10 0,10 10))'

select
  tg_to_wkt(tg_group_ring(tg_point(1, 1)))
from (select 1 where 0); -- 'POLYGON(())'

select tg_group_ring(value) from json_each('[
  "LINESTRING(0 0, 1 1)",
]'); -- error: parameters to tg_group_ring() must be Point geometries
-- #endregion

-- #region tg_group_polygon
-- 1-arg form: first row is the exterior, remaining rows are holes
select
  tg_to_wkt(tg_group_polygon(value))
from json_each('[
  "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))",
  "POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))",
]'); -- 'POLYGON((0 0,10 0,10 10,0 10,0 0),(1 1,2 1,2 2,1 2,1 1))'

-- bare ring rows (e.g. from tg_group_ring()) are accepted too
select
  tg_to_wkt(
    tg_group_polygon(
      tg_ring(tg_point(0, 0), tg_point(10, 0), tg_point(10, 10), tg_point(0, 0))
    )
  ); -- 'POLYGON((0 0,10 0,10 10,0 0))'

select
  tg_to_wkt(tg_group_polygon(tg_ring(tg_point(1, 1))))
from (select 1 where 0); -- 'POLYGON EMPTY'

-- a Polygon WITH holes cannot be used as a ring row: silently using its
-- exterior would drop the holes' data
select tg_group_polygon(value) from json_each('[
  "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0), (1 1, 2 1, 2 2, 1 2, 1 1))",
]'); -- error: argument to tg_group_polygon() expected a ring, found a Polygon with holes (its holes would be silently dropped)

select tg_group_polygon(value) from json_each('[
  "POINT(1 1)",
]'); -- error: argument to tg_group_polygon() expected a ring, found Point
-- #endregion

-- #region tg_group_geometrycollection
-- primary (underscore-free) spelling
select
  tg_to_wkt(
    tg_group_geometrycollection(value)
  )
from json_each('[
  "POINT (1 1)",
  "LINESTRING (2 2, 3 3)",
  "POLYGON ((4 4, 5 5, 6 6, 4 4))",
]'); -- 'GEOMETRYCOLLECTION(POINT(1 1),LINESTRING(2 2,3 3),POLYGON((4 4,5 5,6 6,4 4)))'

select
  tg_to_wkt(
    tg_group_geometrycollection(value)
  )
from json_each('[]'); -- 'GEOMETRYCOLLECTION EMPTY'

select tg_group_geometrycollection(NULL); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).

-- deprecated compatibility alias: tg_group_geometry_collection, same
-- step/final pair, must behave identically
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
  tg_equals(a, b),
  tg_equals(b, a),
  tg_intersects(a, b),
  tg_intersects(b, a),
  tg_touches(a, b),
  tg_touches(b, a),
  tg_within(a, b),
  tg_within(b, a)
from predicate_test_cases; -- @snap predicates

select tg_equals('POINT (1 1)', 'POINT (2 2)'); -- 0
select tg_equals(NULL, 'POINT (1 1)'); -- error: invalid geometry input. Must be WKT (as text), WKB (as blob), or GeoJSON (as text).
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
explain query plan select * from tg_demo1 where tg_touches(_shape, ''); -- @snap tg0-eqp-touches
explain query plan select * from tg_demo1 where tg_equals(_shape, ''); -- @snap tg0-eqp-equals
select rowid, * from tg_demo1; -- @snap tg0-rows-star
-- #endregion

-- #region MISC
create table t as
  select
    tg_extra_json(geometry) as m,
    tg_to_wkb(geometry) as geometry
  from tg_each(readfile('tests/data/collection.geojson'));

select octet_length(m), octet_length(geometry) from t; -- @snap misc-octet-lengths
-- #endregion

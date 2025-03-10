--.load dist/tg0

-- [ ] Converage (functions, vtabs)
-- [ ] authorizater deny
-- [ ] shadow table contents?
-- [ ] fuzzer?

select tg_version();
select tg_debug();

--- # tg_point
  select tg_point(1, 2);
  select subtype(tg_point(1, 2));
  select tg_to_wkt(tg_point(1, 3));
  select tg_to_wkb(tg_point(1, 2));
  select tg_to_geojson(tg_point(1, 2));
  -- errors
    select tg_point('a', 1);
    select tg_point(1, 'a');



--- # tg_points_each
  select 
    rowid, 
    point, 
    tg_to_wkb(point), 
    tg_to_wkt(point), 
    tg_to_geojson(point) 
  from tg_points_each('MULTIPOINT ((10 40), (40 30), (20 20), (30 10))');

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
  join tg_points_each(value);

--- # tg_lines_each
  select 
    rowid, 
    line, 
    tg_to_wkb(line),
    tg_to_wkt(line),
    tg_to_geojson(line)
  from tg_lines_each('MULTILINESTRING ((10 40, 40 30), (20 20, 30 10))');
  
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
  join tg_lines_each(value);
  
  select * from tg_lines_each(NULL);

--- # tg_polygons_each
  select 
    rowid, 
    polygon, 
    tg_to_wkb(polygon),
    tg_to_wkt(polygon),
    tg_to_geojson(polygon)
  from tg_polygons_each('MULTIPOLYGON (((10 40, 40 30, 20 20, 10 40)), ((30 20, 20 10, 10 30, 30 20)))');

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
   join tg_polygons_each(value);

--- # tg_geometries_each
  select 
    rowid, 
    geometry, 
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from tg_geometries_each('GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))');

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
   join tg_geometries_each(value);

--- # tg_mulitpoint
  select tg_to_wkt(
    tg_multipoint(tg_point(1, 2))
  );
  select tg_to_wkt(
    tg_multipoint(
      tg_point(1, 2)
    )
  );
  select tg_to_wkt(
    tg_multipoint(
      tg_point(1, 2),
      tg_point(3, 4)
    )
  );
  select tg_to_wkt(
    tg_multipoint(
      tg_point(1, 2),
      tg_point(3, 4),
      tg_point(5, 6),
      tg_point(7, 8)
    )
  );

  -- errors
  select tg_multipoint(NULL);
  --select tg_multipoint(tg_line);


--- # tg_extra_json
  select tg_extra_json('{"type": "Point","coordinates": [-118.2097812,34.0437074]}');
  
  select tg_extra_json('{"id": "A", "type": "Point","coordinates": [-118.2097812,34.0437074]}');



--- # tg_line
  select tg_line();
  select tg_to_wkt(tg_line());
  select tg_to_wkt(
    tg_line(
      tg_point(1, 2),
      tg_point(3, 4)
    )
  );
  select tg_line(tg_point(1, 1), 'not point');
  select tg_line('LINESTRING EMPTY');
  --select t


--- # tg_type
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
  ]');

  select tg_type(NULL); 


--- # tg_poly_exterior
  select tg_to_wkt(tg_poly_exterior('POLYGON ((1 2, 3 4, 5 6, 1 2))'));
  select tg_to_wkt(tg_poly_exterior('POLYGON ((1 2, 3 4, 5 6, 1 2), (7 8, 9 10, 11 12, 7 8))'));
  --select tg_poly_exterior('POLYGON EMPTY');


--- # tg_holes_each
  select 
    rowid,
    *,
    tg_to_wkt(hole)
  from tg_holes_each('POLYGON ((1 2, 3 4, 5 6, 1 2), (7 8, 9 10, 11 12, 7 8))');

  -- empty
  select 
    rowid,
    *,
    tg_to_wkt(hole)
  from tg_holes_each('POLYGON ((1 2, 3 4, 5 6, 1 2))');

  select 
    tg_holes_each.rowid as "tg_holes_each.rowid",
    json_each.rowid as "json_each.rowid",
    json_each.key,
    tg_to_wkt(hole)
  from json_each('[
    "POLYGON ((1 1, 2 2, 1 1), (3 3, 4 4, 3 3))",
    "POLYGON ((1 1, 2 2, 1 1), (5 5, 6 6, 5 5), (7 7, 8 8, 7 7))",
  ]')
  join tg_holes_each(json_each.value);


--- # tg_each
  select 
    rowid, 
    geometry, 
    tg_to_wkb(geometry),
    tg_to_wkt(geometry),
    tg_to_geojson(geometry)
  from tg_each('GEOMETRYCOLLECTION (POINT (10 20), LINESTRING (30 40, 50 60))');
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
   join tg_each(value);


  --- # tg_valid_geojson
  select tg_valid_geojson('{"type":"Point","coordinates":[0.5,0.5]}');
  select tg_valid_geojson('POINT(0.5 0.5)');
  select tg_valid_geojson(X'0101000000000000000000E03F000000000000E03F');

  --- # tg_valid_wkt
  select tg_valid_wkt('{"type":"Point","coordinates":[0.5,0.5]}');
  select tg_valid_wkt('POINT(0.5 0.5)');
  select tg_valid_wkt(X'0101000000000000000000E03F000000000000E03F');
  
  --- # tg_valid_wkb
  select tg_valid_wkb('{"type":"Point","coordinates":[0.5,0.5]}');
  select tg_valid_wkb('POINT(0.5 0.5)');
  select tg_valid_wkb(X'0101000000000000000000E03F000000000000E03F');

  --- # tg_group_multipoint
  select 
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[
    "POINT(1 1)",
    "POINT(2 2)",
    "POINT(3 3)",
  ]');

  select 
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[]');
  
  select 
    tg_to_wkt(tg_group_multipoint(value))
  from json_each('[
    "POINT (1 1)",
    "LINESTRING (2 2, 3 3)",
  ]');

  --- # tg_group_multipolygon
  select
    tg_to_wkt(tg_group_multipolygon(value))
  from json_each('[
    "POLYGON ((1 1, 2 2, 3 3, 1 1))",
    "POLYGON ((4 4, 5 5, 6 6, 4 4))",
    "POLYGON ((7 7, 8 8, 9 9, 7 7))",
  ]');

  select
    tg_to_wkt(tg_group_multipolygon(value))
  from json_each('[]');

  select
    tg_to_wkt(tg_group_multipolygon(value))
  from json_each('[
    "POLYGON ((1 1, 2 2, 3 3, 1 1))",
    "POINT (3 3)",
  ]');

  -- TODO rename _final, w/ code that notices and flags
  --- # tg_group_multilinestring_final
  select 
    tg_to_wkt(
      tg_group_multilinestring(value)
    )
  from json_each('[
    "LINESTRING (1 1, 2 2)",
    "LINESTRING (3 3, 4 4)",
    "LINESTRING (5 5, 6 6)",
  ]');
  
  select 
    tg_to_wkt(
      tg_group_multilinestring(value)
    )
  from json_each('[]');
  
  select tg_group_multilinestring(NULL);
  select tg_group_multilinestring('POINT (1 1)');

    --- # tg_group_geometry_collection
  select 
    tg_to_wkt(
      tg_group_geometry_collection(value)
    )
  from json_each('[
    "POINT (1 1)",
    "LINESTRING (2 2, 3 3)",
    "POLYGON ((4 4, 5 5, 6 6, 4 4))",
  ]');
  
  select 
    tg_to_wkt(
      tg_group_geometry_collection(value)
    )
  from json_each('[]');

  select tg_group_geometry_collection(NULL);


--- tg_group_feature_collection_geojson
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
]');

--- # tg_group_bbox
select 
  tg_to_wkt(tg_group_bbox(value))
from json_each('[
  "POINT (1 1)",
  "LINESTRING (2 2, 3 3)"
]');


  --- # tg_bbox
  select * from tg_bbox('POINT (1 1)');
  select * from tg_bbox('POLYGON ((1 1, 2 2, 3 3, 1 1))');
  select * from tg_bbox(NULL);
  select 
    value, 
    tg_bbox.*
  from json_each('[
    "POINT (1 1)",
    "POLYGON ((1 1, 2 2, 3 3, 1 1))",
    "MULTIPOLYGON (((1 1, 2 2, 3 3, 1 1)), ((4 4, 5 5, 6 6, 4 4)))",
    "LINESTRING (4 4, 6 6, 7 7)",
  ]')
  join tg_bbox(value);


  --- # predicates
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
  from predicate_test_cases;


--- # tg_geom
select tg_geom('POINT (1 1)');
select tg_geom('POINT (1 1)', 'none');
select tg_geom('POINT (1 1)', 'natural');
select tg_geom('POINT (1 1)', 'ystripes');
select tg_geom('POINT (1 1)', 'unknown');



--- # tg0
create virtual table temp.tg_demo1 using tg0();
select name from temp.sqlite_master where name like 'tg%' order by 1;
insert into tg_demo1(rowid, _shape) select key, value from json_each('[
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818620800527,32.881627962039275],[-117.23803891594858,32.881627962039275],[-117.23803891594858,32.88150426716983],[-117.23818620800527,32.88150426716983],[-117.23818620800527,32.881627962039275]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23794407791227,32.88163023398593],[-117.23779678585558,32.88163023398593],[-117.23779678585558,32.88150653911649],[-117.23794407791227,32.88150653911649],[-117.23794407791227,32.88163023398593]]]},"properties":{},},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.23818630582224,32.881454314415976],[-117.23803901376554,32.881454314415976],[-117.23803901376554,32.88133061954653],[-117.23818630582224,32.88133061954653],[-117.23818630582224,32.881454314415976]]]},"properties":{}},
    {"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-117.2379432340281,32.881454247902326],[-117.2377959419714,32.881454247902326],[-117.2377959419714,32.88133055303288],[-117.2379432340281,32.88133055303288],[-117.2379432340281,32.881454247902326]]]},"properties":{}},
]');
select count(*) from tg_demo1_rtree;
select rowid, typeof(_shape), tg_to_wkt(_shape) from tg_demo1;
select id, minX, maxX, minY, maxY from tg_demo1_rtree;
explain query plan select * from tg_demo1;
explain query plan select * from tg_demo1 where tg_intersects(_shape, '');
explain query plan select * from tg_demo1 where tg_contains(_shape, '');
select rowid, * from tg_demo1;

  --- # MISC
  create table t as 
    select 
      tg_extra_json(geometry) as m, 
      tg_to_wkb(geometry) as geometry 
    from tg_each(readfile('examples/us-states.geojson'));

  select octet_length(m), octet_length(geometry) from t;


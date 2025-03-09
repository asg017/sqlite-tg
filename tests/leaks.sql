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


--- # tg_group_multipoint
select tg_to_wkt(
  tg_group_multipoint(tg_point(1, 2))
);

select 
  tg_to_wkt(
    tg_group_multipoint(
      tg_point(value, value)
    )
  ) 
from json_each('[1,2,3,4,5]');

-- empty state
select tg_to_wkt(
  tg_group_multipoint(
    tg_point(value, value)
  )
) 
from json_each('[]');

-- TODO empty state?

-- Errors
select tg_group_multipoint(NULL);
select tg_group_multipoint('MULTIPOINT EMPTY');

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



  -- # MISC
  create table t as 
    select 
      tg_extra_json(geometry) as m, 
      tg_to_wkb(geometry) as geometry 
    from tg_each(readfile('examples/us-states.geojson'));

  select octet_length(m), octet_length(geometry) from t;


.load dist/tg0

.mode box
.header on
.bail on

select tg_geom('LINESTRING (10 10, 20 20, 10 40)');
select tg_geom('LINESTRING (10 10, 20 20, 10 40)', 'none');
select tg_geom('LINESTRING (10 10, 20 20, 10 40)', 'natural');
select tg_geom('LINESTRING (10 10, 20 20, 10 40)', 'ystripes');
select tg_geom('LINESTRING (10 10, 20 20, 10 40)', 'notexist');

select
  rowid,
  polygon,
  tg_to_wkt(polygon),
  tg_to_geojson(polygon)
from tg_polygons_each('
MULTIPOLYGON (((40 40, 20 45, 45 30, 40 40)),
((20 35, 10 30, 10 10, 30 5, 45 20, 20 35),
(30 20, 20 15, 20 25, 30 20)))
');



select *
from tg_rect_parts('{"type":"Polygon","coordinates":[[[-118.11841053556675,34.02658161934947],[-118.10179269188654,34.02658161934947],[-118.10179269188654,34.01438223897489],[-118.11841053556675,34.01438223897489],[-118.11841053556675,34.02658161934947]]]}');

select
  tg_geometries_each.rowid,
  geometry,
  tg_extra_json(geometry),
  minX,
  maxX,
  minY,
  maxY
from tg_geometries_each('{
  "type":"FeatureCollection",
  "features":[
    {"id": "a", "type":"Feature","properties":{"color": "red"},"geometry":{"type":"LineString","coordinates":[[-118.149747,34.0273686],[-118.1105764,34.0084787],[-118.1105764,34.0084787],[-118.1105764,34.0084787]]}},
    {"id": "b", "type":"Feature","properties":{"color": "blue"},"geometry":{"type":"Point","coordinates":[-118.1376398,34.0315002]}},
    {"id": "c", "type":"Feature","properties":{"color": "green"},"geometry":{"type":"Point","coordinates":[-118.1281438,34.0313035]}},
    {"id": "d", "type":"Feature","properties":{"color": "yellow"},"geometry":{"type":"Polygon","coordinates":[[[-118.11841053556675,34.02658161934947],[-118.10179269188654,34.02658161934947],[-118.10179269188654,34.01438223897489],[-118.11841053556675,34.01438223897489],[-118.11841053556675,34.02658161934947]]]}}
  ]
}')
inner join tg_rect_parts(geometry);
.exit


select tg_extra_json('{"type":"Point","coordinates":[-118.1376398,34.0315002]}');
select tg_extra_json('{"id": "aaa", "type":"Point","coordinates":[-118.1376398,34.0315002]}');


select
  rowid,
  geometry,
  tg_extra_json(geometry),
  tg_to_wkt(geometry),
  tg_to_geojson(geometry)
from tg_geometries_each('{
  "type":"FeatureCollection",
  "features":[
    {"id": "a", "type":"Feature","properties":{"color": "red"},"geometry":{"type":"LineString","coordinates":[[-118.149747,34.0273686],[-118.1105764,34.0084787],[-118.1105764,34.0084787],[-118.1105764,34.0084787]]}},
    {"id": "b", "type":"Feature","properties":{"color": "blue"},"geometry":{"type":"Point","coordinates":[-118.1376398,34.0315002]}},
    {"id": "c", "type":"Feature","properties":{"color": "green"},"geometry":{"type":"Point","coordinates":[-118.1281438,34.0313035]}},
    {"id": "d", "type":"Feature","properties":{"color": "yellow"},"geometry":{"type":"Polygon","coordinates":[[[-118.11841053556675,34.02658161934947],[-118.10179269188654,34.02658161934947],[-118.10179269188654,34.01438223897489],[-118.11841053556675,34.01438223897489],[-118.11841053556675,34.02658161934947]]]}}
  ]
}');

.exit

select
  rowid,
  geometry,
  tg_to_wkt(geometry),
  tg_to_geojson(geometry)
from tg_geometries_each('GEOMETRYCOLLECTION (POINT (40 10),
LINESTRING (10 10, 20 20, 10 40),
POLYGON ((40 40, 20 45, 45 30, 40 40)))');





select
  rowid,
  line,
  tg_to_wkt(line),
  tg_to_geojson(line)
from tg_lines_each('MULTILINESTRING ((10 10, 20 20, 10 40), (40 40, 30 30, 40 20, 30 10))');

select
  rowid,
  point,
  tg_to_wkt(point),
  tg_to_geojson(point)
from tg_points_each('MULTIPOINT ((10 40), (40 30), (20 20), (30 10))');


.exit

select tg_type('MULTIPOINT ((90 80), (70 60))') as a;
select tg_type('MULTIPOINT (90 80,70 60,50 40)') as b;

select
  tg_points_each.rowid,
  point,
  subtype(point),
  tg_to_wkt(point),
  tg_to_geojson(point)
from json_each('[
 "MULTIPOINT ((10 20), (30 40))",
 "MULTIPOINT (90 80,70 60,50 40)"
]')
join tg_points_each(value);



.exit

select
  rowid,
  point,
  tg_to_wkt(point),
  tg_to_geojson(point)
from tg_points_each('MULTIPOINT ((10 40), (40 30), (20 20), (30 10))');

.exit
select tg_memsize(tg_point(1,2));
select tg_memsize(tg_point(1,2));
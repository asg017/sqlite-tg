.load ../../../sqlite-xsv/dist/debug/xsv0.dylib
.load ../../dist/tg0

create table colleges as
select
  row ->> 'UNITID' as id,
  row ->> 'INSTNM' as name,
  row ->> 'LATITUDE' as latitude,
  row ->> 'LONGITUDE' as longitude
from csv_rows(readfile('Most-Recent-Cohorts-Institution.csv'));

create table native_territories as
select value
from json_each(readfile('native-lands-territories.geojson'));


select * from colleges limit 2;
select * from native_territories limit 2;

create virtual table tree_index using rtree(
  id, minX, maxX, minY, maxY,
  +boundary, +extra
);


select rowid, value
from native_territories
where not tg_valid_geojson(value)
limit 3;

select tg_geom(value)
from native_territories
where not tg_valid_geojson(value);
.exit

insert into tree_index
select
  native_territories.rowid,
  bbox.minX,
  bbox.maxX,
  bbox.minY,
  bbox.maxY,
  tg_to_wkb(value),
  tg_extra_json(value)
from native_territories
inner join tg_rect_parts(value) as bbox;

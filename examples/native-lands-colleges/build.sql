.load ../../../sqlite-xsv/dist/debug/xsv0.dylib
.load ../../dist/tg0
.bail on

-- College Scorecard institution data: https://collegescorecard.ed.gov/data/
create table colleges as
select
  row ->> 'UNITID' as id,
  row ->> 'INSTNM' as name,
  cast(row ->> 'LATITUDE' as real) as latitude,
  cast(row ->> 'LONGITUDE' as real) as longitude
from csv_rows(readfile('Most-Recent-Cohorts-Institution.csv'));

-- native-land.ca territory shapes mix Polygon and MultiPolygon features,
-- so the geometry column stays the generic `geometry` type.
create virtual table territories using tg0(
  name text,
  extra text,
  boundary geometry
);

insert into territories(rowid, name, extra, boundary)
  select
    territory.rowid,
    territory.value ->> '$.properties.Name' as name,
    tg_extra_json(territory.value) as extra,
    tg_to_wkb(territory.value) as boundary
  from json_each(readfile('native-lands-territories.geojson')) as territory;

.mode box
.header on

-- which territories is each college on? The point-in-polygon lookup is
-- accelerated by the tg0 table's R-Tree index on `boundary`
select
  colleges.name as college,
  territories.name as territory
from colleges
join territories
  on tg_intersects(territories.boundary, tg_point(colleges.longitude, colleges.latitude))
limit 20;

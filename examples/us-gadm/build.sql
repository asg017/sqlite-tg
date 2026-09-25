.bail on
.load ../../dist/tg0

-- GADM 4.1 United States level-2 (county) boundaries. Every feature in the
-- source GeoJSON is a MultiPolygon, so the geometry column can be narrowed.
create virtual table us_counties using tg0(
  id integer primary key,
  state_name text,
  county_name text,
  boundary multipolygon
);

insert into us_counties(id, state_name, county_name, boundary)
  select
    rowid,
    tg_extra_json(geometry) ->> '$.properties.NAME_1' as state_name,
    tg_extra_json(geometry) ->> '$.properties.NAME_2' as county_name,
    tg_to_wkb(geometry) as boundary
  from tg_geometries_each(cast(readfile('gadm41_USA_2.json') as text));

.param set :query '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-114.69187471576565,35.04749579636169],[-114.56679529479798,35.04749579636169],[-114.56679529479798,34.97196634479279],[-114.69187471576565,34.97196634479279],[-114.69187471576565,35.04749579636169]]]}}'
.mode box
.header on

-- counties whose boundary intersects the query polygon: the bbox prefilter
-- runs through the tg0 table's R-Tree index automatically
select id, state_name, county_name
from us_counties
where tg_intersects(boundary, :query);

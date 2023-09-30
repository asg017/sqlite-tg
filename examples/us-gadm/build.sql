.bail on
.load ../../dist/tg0

--.mode box

create table us_counties as
select
  tg_extra_json(geometry) ->> '$.properties.NAME_1' as state_name,
  tg_extra_json(geometry) ->> '$.properties.NAME_2' as country_name,
  tg_to_wkb(geometry) as geometry
from tg_geometries_each(cast(readfile('gadm41_USA_2.json') as text));


create virtual table rtree_us_counties using rtree(id, minX, maxX, minY, maxY,/* +boundary*/);

insert into rtree_us_counties
  select
    us_counties.rowid,
    bbox.*--,
    --geometry
  from us_counties
  join tg_rect_parts(geometry) as bbox;



.param set :query '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-114.69187471576565,35.04749579636169],[-114.56679529479798,35.04749579636169],[-114.56679529479798,34.97196634479279],[-114.69187471576565,34.97196634479279],[-114.69187471576565,35.04749579636169]]]}}'
.mode box
.header on

with candidates as (
  select
    rtree_us_counties.id,
    --rtree_us_counties.boundary,
    us_counties.state_name,
    us_counties.country_name
  from rtree_us_counties, tg_rect_parts(:query) as query
  left join us_counties on us_counties.rowid = rtree_us_counties.id
  where rtree_us_counties.minX <= query.maxX and rtree_us_counties.maxX >= query.minX
    and rtree_us_counties.minY <= query.maxY and rtree_us_counties.maxY >= query.minY
)
select * from candidates;

.load ../../dist/tg0
.timer on
.bail on

--create table buildings as

create virtual table rtree_buildings using rtree(
  id, minX, maxX, minY, maxY,
  +boundary, +extra
);


insert into rtree_buildings
select
  building.rowid,
  rect.*,
  tg_to_wkb(geometry) as boundary,
  tg_extra_json(geometry) as extra
from tg_geometries_each(cast(readfile('Building_Footprints.geojson') as text)) as building
inner join tg_rect_parts(geometry) as rect;


--select json_type(cast(readfile('Building_Footprints.geojson') as text));

--118.0589095,33.964059

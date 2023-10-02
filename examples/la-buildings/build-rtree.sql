.load ../../dist/tg0
.timer on
.bail on

attach database "base.db" as base;

create virtual table rtree_buildings using rtree(
  id, minX, maxX, minY, maxY,
  +boundary, +extra
);


insert into rtree_buildings
select
  building.rowid,
  tg_bbox.*,
  boundary,
  extra
from base.base as building
inner join tg_bbox(boundary);

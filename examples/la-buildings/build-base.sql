.load ../../dist/tg0

create table base as
select
  building.rowid,
  tg_extra_json(geometry) as extra,
  tg_to_wkb(geometry) as boundary
from tg_geometries_each(cast(readfile('Building_Footprints.geojson') as text)) as building;

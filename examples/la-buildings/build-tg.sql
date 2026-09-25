.load ../../dist/tg0
.timer on
.bail on

attach database "base.db" as base;

-- The LA Building Footprints GeoJSON mixes single- and multi-part
-- building shapes, so the geometry column stays generic `geometry`.
create virtual table tg_buildings using tg0(
  boundary geometry,
  extra text
);

insert into tg_buildings(rowid, boundary, extra)
  select
    rowid,
    boundary,
    extra
  from base.base;

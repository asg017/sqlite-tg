.load ../../dist/tg0
.timer on
.bail on

attach database "base.db" as base;

create virtual table tg_buildings using tg0(extra);

insert into tg_buildings(rowid, _shape, extra)
  select
    rowid,
    boundary,
    extra
  from base.base;

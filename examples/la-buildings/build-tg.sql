.load ../../dist/tg0
.timer on
.bail on

attach database "base.db" as base;

create table buildings(id int primary key, extra json);

insert into buildings(id, extra)
  select rowid, extra from base.base;


create virtual table tg_buildings using tg0();
begin;
insert into tg_buildings(rowid, _shape)
  select
    rowid,
    boundary
  from base.base;
commit;

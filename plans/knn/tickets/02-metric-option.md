# 02 — `metric=` geometry-column option and coordinate validation

Decision 5 / open questions 1 and 5. Blocked on Alex confirming the option
name and default; everything below assumes `metric=geodesic|planar`,
default geodesic.

## Tasks

1. Parser (`tg0_parse_geometry_column` region, sqlite-tg.c ~2926-3020 in
   the tg0 worktree): accept `metric=geodesic|planar` alongside
   `indexing=`; store `tg_metric` on `tg0_geometry_column`. Error text for
   unknown values lists both. Reject `metric=` on data columns the way
   `indexing=` is rejected.
2. Write path (`tg0_write_insert` / `tg0_write_update`): for geodesic
   columns, after `tg_geom_rect()`, check the bbox with `tg_lonlat_valid`
   (from 01). Failure → `SQLITE_CONSTRAINT`, message
   `"%s.%s: coordinates outside lon/lat range (x=%g, y=%g); declare the
   column with metric=planar for projected coordinates"`. Planar columns:
   reject non-finite coordinates only.
3. `tg0_info` shadow table: nothing new stored (the option is re-parsed on
   xConnect). Note this in the region comment.
4. Snapshot tests: create with each value, bad value, option on data column,
   insert out-of-range into geodesic vs planar, NaN into either.
5. `reference.yaml` tg0 entry: document the option (rendered by
   `make docs`).

## Acceptance

- Existing tg0 tests unchanged except new snapshots.
- `create virtual table t using tg0(g point)` then `insert ... 500000, 4e6`
  fails with the message above; with `metric=planar` it succeeds.

# tg0 v2 — release notes snippet

## ⚠️ Breaking change: `tg0` tables now use typed columns

The `tg0` virtual table was rebuilt. The old constructor —
`tg0(aux1, aux2, ...)`, untyped auxiliary columns next to an implicit
`_shape` geometry column — and its storage layout are gone. **New builds
cannot open tg0 tables created by older versions**; they fail with a
`tg0 constructor error` and a migration hint. There is no in-place
migration: dump old tables with the extension version that created them
(`create table dump as select rowid, _shape, ... from old_table`) or
re-fetch the source data, then recreate and re-insert.

Before:

```sql
create virtual table businesses using tg0(name);
insert into businesses(rowid, _shape, name)
  values (1, 'POINT(-122.4075 37.787994)', 'in sf');
select rowid, name from businesses where tg_intersects(_shape, :area);
```

After:

```sql
create virtual table businesses using tg0(
  id integer primary key,
  name text,
  location point
);
insert into businesses(id, name, location)
  values (1, 'in sf', 'POINT(-122.4075 37.787994)');
select id, name from businesses where tg_intersects(location, :area);
```

## New in tg0

- **Typed columns.** Geometry columns are declared `geometry` or narrowed to
  one of the 7 concrete types (`point`, `linestring`, `polygon`,
  `multipoint`, `multilinestring`, `multipolygon`, `geometrycollection`),
  `NOT NULL` and strictly enforced on write. Up to 8 geometry columns per
  table, each with its own R-Tree; plus up to 16 nullable, type-enforced
  data columns (`text | int | integer | float | double | blob`).
- **Integer and text primary keys.** `id integer primary key` aliases the
  rowid; `id text primary key` declares a `WITHOUT ROWID` table keyed by
  text ids.
- **All 8 predicates accelerated.** `tg_contains`, `tg_within`, `tg_covers`,
  `tg_coveredby`, and `tg_disjoint` used to error at query time on tg0
  tables; every predicate now plans through the R-Tree (disjoint is an
  exact-test full scan by design), and multiple predicates per `WHERE`
  clause are allowed.
- **Faster storage layout.** Row data lives in a content shadow table, and
  the R-Trees hold only bounding boxes — rowid/primary key lookups are
  point queries instead of full scans.
- **`indexing=none|natural|ystripes`** per geometry column controls the tg
  polygon index used when parsing stored geometries at query time.
- **UPDATE support** for geometry and data columns (changing a row's
  rowid/primary key is not supported). `last_insert_rowid()` now works
  after INSERTs.
- **Data-column constraint pushdown**: `=`, `!=`, `<`, `<=`, `>`, `>=` on
  data columns filter inside the content scan before geometries are parsed.
- **`ALTER TABLE ... RENAME TO`** and **`PRAGMA integrity_check`** support.
- **KNN reserved, not implemented**: a hidden `distance` column and a plan
  code reserve the surface; `MATCH`/`distance` constraints error with
  "KNN queries are not supported on tg0 tables yet".

tg0 remains experimental — expect further breaking changes (KNN and
partition keys are planned).

# 07 — Docs, reference, site, examples migration

Make the new tg0 the documented, exemplified default (decision 2 — clean
break includes migrating everything in-repo).

## Tasks

1. `reference.yaml`: rewrite the `virtual-tables: tg0:` entry — full
   constructor grammar, column kinds, per-predicate acceleration table,
   NOT NULL + strict-typing rules, pk flavors, `indexing=` option, UPDATE
   support, "KNN reserved" note. Then regenerate: `make docs` +
   `make docs-check`, `node site/build-ref.mjs` for `site/api-reference.md`.
2. `docs.md` prose: replace the single tg0 paragraph with a real section
   (worked example: create, insert GeoJSON, point-in-polygon query with
   EXPLAIN QUERY PLAN, update, text-pk example). Keep "experimental" framing
   but drop "expect breaking changes" only if we mean it — otherwise keep.
3. Site: add tg0 to getting-started/introduction where relevant; consider a
   playground sample that uses tg0 (site/playground datasets are plain
   tables today — optional, note as follow-up if skipped).
4. Migrate examples to the new syntax, using typed columns where the data
   allows:
   - `examples/timezone/build.sql` — adopt its own aspirational form:
     `tg0(id integer primary key, tzid text, timezone multipolygon)` (verify
     the dataset is all MultiPolygon; else `geometry`).
   - `examples/la-buildings/build-tg.sql`, `examples/us-gadm/build.sql`,
     `examples/native-lands-colleges/build.sql`,
     `examples/microsoft-buildings/buildings.sql`.
   Run each example's build to verify.
5. CHANGELOG/release notes snippet: breaking-change callout with a
   before/after migration example (CREATE + INSERT ... SELECT from old
   table's `_shape`).

## Acceptance

- `make docs-check` passes; `site/api-reference.md` regenerated (not
  hand-edited); site builds (`cd site && npm run docs:build`).
- All migrated examples build against the new extension.
- NOTES checkboxes flipped for shipped items.

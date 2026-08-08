# 04 — Docs content

Port existing prose docs into real site pages; generate the API reference from
`reference.yaml`.

Depends on: 01. Parallel-safe with 02/03.

## Sources (read all first)

- `docs.md` (repo root) — supported formats, pointer-functions explainer, and
  per-function prose. Some content may be stale relative to `sqlite-tg.c`.
- `reference.yaml` — sections (meta/constructors/validate/convertors/predicates)
  + function docs with examples.
- `sqlite-tg.c` (~line 3355 onward) — the actual registered function list.
  Functions registered but missing from reference.yaml (e.g. `tg_equals`,
  accessors like `tg_x/tg_y/tg_z/tg_m/tg_has_z/tg_has_m`,
  `tg_min_x/tg_min_y/tg_max_x/tg_max_y`, `tg_dims`, `tg_is_empty`,
  `tg_is_feature`, `tg_is_featurecollection`, `tg_memsize`, `tg_linestring`,
  `tg_geometrycollection`, `tg_bbox`, table functions) should get at least a
  stub entry (add them to `reference.yaml` with a one-line desc + example —
  check the C code/tests in `tests/` for behavior).
- `/Users/alex/projects/sqlite-vec/site/build-ref.mjs` — reference generator to
  adapt (reads reference.yaml → emits api-reference markdown). Simplify as
  needed; sqlite-vec's uses better-sqlite3 to execute examples — SKIP example
  execution for now (no output blocks), just render desc + example SQL.

## Tasks

1. `site/build-ref.mjs` (+ `npm run ref` script): generates
   `site/api-reference.md` from `../reference.yaml`: grouped by section, `###
   tg_func(args)` headings with stable anchors, desc, ` ```sql ` example
   blocks. Commit the generated file (like sqlite-vec does). Wire `ref` into
   the `docs:build` script (`npm run ref && vitepress build`).
2. `site/getting-started/introduction.md` — what sqlite-tg is (thin wrapper
   around [tg](https://github.com/tidwall/tg)), pre-v1 warning, supported
   formats section + pointer-functions explainer ported from `docs.md`.
3. `site/getting-started/installation.md` — frontloads the
   `installing.md.tmpl` include; check the tmpl still renders.
4. Home `site/index.md`: keep hero + the point-in-polygon example; add a
   3-item features block (Fast point-in-polygon / WKT-WKB-GeoJSON conversions /
   Runs anywhere incl. WASM). Keep the `#installing` include section.
5. Wire `rewrites` in config like sqlite-vec (`getting-started/:pkg.md` →
   `:pkg.md`) and update sidebar links accordingly.
6. Remove `site/using-with.md` (stale stub) or turn it into a real
   "Using with…" page only if content exists in README — don't invent bindings.

## Acceptance

- `npm run docs:build` passes with no dead links (remove `ignoreDeadLinks` if
  ticket 01 added it, unless /playground still pending — stub it if so).
- `api-reference.md` covers every function registered in `sqlite-tg.c`
  (verify by grepping the built page for each registered name).
- Site renders: home, introduction, installation, api-reference.

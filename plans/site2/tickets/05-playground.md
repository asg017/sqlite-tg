# 05 — Playground page

The star feature: `/playground` — a SQL editor running sqlite-tg (WASM) fully
client-side, with sample databases.

Depends on: 02 (wasm in public/), 03 (datasets module). Read both tickets'
results first.

## UX

- Full-width page (`site/playground.md` with frontmatter
  `layout: page` + `sidebar: false`), rendering `<PlaygroundPage />` inside
  `<ClientOnly>`.
- Layout: toolbar (Run button, dataset picker, status/timing) above a
  CodeMirror 6 SQL editor, results below. Keyboard: Cmd/Ctrl+Enter runs.
- Results: one HTML table per result-producing statement (column headers +
  rows). BLOB values rendered as hex with `X'...'` styling, NULL as italic
  `NULL`. Errors shown in a styled error box with the message. Show elapsed ms
  and row counts.
- Dataset picker: "(empty database)" default + entries from
  `datasets.json` (ticket 03). Picking one resets the DB, loads the dataset,
  and shows a schema summary (table name + columns + row count) and pre-fills
  the editor with the dataset's example query if the manifest has one (add an
  optional `exampleSql` field to the manifest + entries).
- On mount: read `?sql=<base64url>` and optional `&db=<dataset-id>` from
  `location.search` (contract from plan.md: base64url no-padding UTF-8).
  If present: pre-fill editor, select/load dataset, and auto-run once loaded.
- A "share" button that copies a `?sql=`/`&db=` URL for the current editor
  content to the clipboard.

## Implementation notes

- npm deps (site/package.json): `codemirror`, `@codemirror/lang-sql`; theme
  via `@codemirror/view` base + CSS variables so it follows VitePress
  light/dark (`useData().isDark` watcher swapping a compartment is fine, or a
  simple CSS-variable-based theme — keep it lean).
- WASM init (see `examples/wasm/test-wasm.ts` and `site/index.html` git
  history for prior art):
  ```ts
  const { default: init } = await import(/* @vite-ignore */ withBase("/playground/sqlite3.mjs"));
  const sqlite3 = await init();
  db = new sqlite3.oo1.DB(":memory:");
  ```
  `withBase` from `vitepress`. Handle init failure with a visible error (e.g.
  artifacts missing in dev → tell user to run `make site-wasm`).
- Executing SQL: iterate statements properly so each SELECT gets its own
  table. Use the C-API prepare loop (`sqlite3.capi.sqlite3_prepare_v2` /
  oo1's `db.prepare` + `sqlite3.capi.sqlite3_complete` alternatives are
  fiddly) — simplest robust approach with oo1:
  ```ts
  let rest = sql;
  while (rest.trim()) {
    const stmt = db.prepare(rest);           // oo1 prepares first stmt
    rest = ???                               // oo1 doesn't expose tail — so instead:
  }
  ```
  oo1 does NOT expose the tail pointer, so use the lower-level API:
  `sqlite3.capi.sqlite3_prepare_v3` with `pzTail` via
  `sqlite3.wasm.scopedAllocPtr`... If that gets gnarly, acceptable fallback:
  `db.exec({sql, rowMode:"object", callback})` collecting rows grouped by
  `columnNames` changes — BUT verify multi-statement behavior; a single
  combined result set for multi-SELECT input is acceptable for v1 if each
  approach above proves too fragile. Prefer the prepare-loop; sqlite3.wasm has
  helpers (`sqlite3.wasm.allocPtr`, `cstrToJs`, etc.). Look at how the bundle's
  own demo apps do it (the sqlite wasm distro contains `demo-123.js`-style
  examples inside `dist/.build/sqlite-src-*/ext/wasm/` — reference them).
- Statement execution must be wrapped in try/finally with `stmt.finalize()`;
  cap rendered rows (e.g. 1000) with a "truncated" notice.
- Component lives in `site/.vitepress/theme/playground/PlaygroundPage.vue`
  (+ small child components as needed). Register nothing globally; the page
  imports it directly.
- SSR safety: the .vue file is only touched client-side via `<ClientOnly>` +
  `defineAsyncComponent(() => import(...))` in the page. `npm run docs:build`
  must not evaluate sqlite3.mjs.

## Acceptance

- `make site-wasm && cd site && npm run docs:dev` → `/playground` runs
  `select tg_version();` and shows a result table.
- Load "US States", run the home-page query
  (point-in-polygon with `tg_intersects` — adapt to the loaded table:
  `select name from states where tg_intersects(geometry, tg_point(-111.02, 35.03));`)
  → returns Arizona.
- `?sql=c2VsZWN0IHRnX3ZlcnNpb24oKTs&db=us-states` pre-fills, loads, auto-runs.
- Multi-statement input (`create table t(a); insert ...; select ...;`) works.
- SQL errors render as error box, not console-only.
- `npm run docs:build` passes (SSR-safe).

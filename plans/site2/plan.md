# sqlite-tg docs site v2 ("site2")

Build a proper VitePress docs site for sqlite-tg, modeled on the sqlite-vec site
(`../sqlite-vec/site`), with one big addition: an interactive **Playground** page
(SQL editor running sqlite-tg via WASM, with built-in sample databases), plus
"Open in playground" buttons on SQL code samples throughout the docs.

Branch: `asg017/site2`. Tickets live in `plans/site2/tickets/*.md` and are worked
in order (see Dependency order below).

## Current state

- `site/` is a half-finished copy of sqlite-vec's site: `config.mts` still says
  `PROJECT = "sqlite-vec"`, description is "A VitePress Site", nav has a dead
  `/playground` link, and `site/index.html` is an unused playground prototype
  that references a nonexistent `slim.js`. Deps: vitepress ^1.6.3, vue ^3.5.13,
  vitepress-plugin-tabs ^0.6.0.
- Content sources of truth:
  - `reference.yaml` — structured API reference (sections + 27 functions with
    desc/params/example). sqlite-vec generates its `api-reference.md` from an
    equivalent file via `site/build-ref.mjs`.
  - `docs.md` — prose docs (supported formats, pointer functions, function docs).
  - `site/installing.md.tmpl` — install instructions include.
- WASM: `make wasm` produces `dist/.wasm/sqlite3.mjs` + `dist/.wasm/sqlite3.wasm`
  — the official SQLite WASM bundle (3.53.4) with sqlite-tg statically linked via
  `sqlite3_wasm_extra_init`. Verified working via `make test-wasm` (Deno). CI
  (`.github/workflows/test.yaml`) builds it with emsdk pinned to 5.0.4.
- No `site-*` targets exist in the Makefile yet. sqlite-vec's deploy workflow
  (`.github/workflows/site.yaml`) runs `npm ci` + `make site-build` and deploys
  `site/.vitepress/dist` to GitHub Pages.

## Decisions

- **VitePress `^1.6.4`** — latest stable. VitePress 2 is still alpha
  (2.0.0-alpha.19 on the `next` tag); not using it.
- **Site shape mirrors sqlite-vec**: hero home page, sidebar with Getting
  Started / Documentation sections, top nav (API Reference, **Playground**,
  Sponsor, version dropdown with bindings links), local search, edit links,
  `base: "/sqlite-tg/"`, MIT/Apache-2 footer, Zilla-Slab-style title treatment
  (reuse sqlite-vec `style.css` as a starting point, swap branding).
- **Playground engine**: reuse `dist/.wasm/sqlite3.mjs` + `sqlite3.wasm`
  unmodified. They are copied (not committed) into `site/public/playground/` by
  a Makefile target; the component dynamic-imports the `.mjs` at runtime with
  `import(/* @vite-ignore */ withBase("/playground/sqlite3.mjs"))`. In-memory
  DBs only (`new sqlite3.oo1.DB(":memory:")`) — no OPFS, no COOP/COEP needed.
- **Editor**: CodeMirror 6 (`codemirror`, `@codemirror/lang-sql`,
  `@codemirror/theme-one-dark` or CSS-variable theming). All playground UI is a
  client-only Vue component (`<ClientOnly>` + dynamic import) since VitePress
  SSRs pages at build time.
- **Sample databases**: shipped as GeoJSON files in
  `site/public/playground/data/` + a `datasets.json` manifest; loaded client
  side by fetching the GeoJSON and inserting one row per feature
  (`rowid, name, geometry` where geometry is the GeoJSON `Feature.geometry`
  serialized as text — usable directly by every `tg_*` function). No native
  build step needed.
- **"Open in playground" link format**: `<base>/playground.html?sql=<b64>` where
  `<b64>` is base64url (RFC 4648 §5, no padding) of the UTF-8 SQL text, plus
  optional `&db=<dataset-id>`. The playground reads the params on mount. This
  format is the contract between tickets 05 and 06.
- **Fence buttons**: a markdown-it fence-renderer wrapper in `config.mts` that
  appends an "Open in playground" anchor to every ` ```sql ` / ` ```sqlite `
  block, computing the href at build time (Node `Buffer` base64url). Opt-out by
  adding `no-playground` to the fence info string. Sidesteps SSR/hydration
  entirely — it's just a rendered `<a>`.
- **Deploy**: new `.github/workflows/site.yaml` modeled on sqlite-vec's, with an
  added emsdk 5.0.4 step + `make wasm` before `make site-build` (Pages needs the
  WASM artifacts). `make site-build` = copy wasm into public + generate
  api-reference + `vitepress build`.

## Tickets

| # | file | summary |
|---|------|---------|
| 01 | `tickets/01-scaffold.md` | Clean VitePress 1.6.4 scaffold: config, theme, nav/sidebar, remove dead files |
| 02 | `tickets/02-wasm-pipeline.md` | Makefile `site-wasm` / `site-build` targets; wasm artifacts into `site/public/playground/` |
| 03 | `tickets/03-datasets.md` | Sample GeoJSON datasets + `datasets.json` manifest + client loader module |
| 04 | `tickets/04-content.md` | Port `docs.md`/`reference.yaml` into real pages; generated API reference; home page |
| 05 | `tickets/05-playground.md` | Playground page: CodeMirror editor, run, results, dataset picker, `?sql=` param |
| 06 | `tickets/06-open-in-playground.md` | "Open in playground" button on SQL fences (markdown-it wrapper) |
| 07 | `tickets/07-deploy.md` | GitHub Pages deploy workflow with emsdk + wasm build |

## Dependency order

```
01 scaffold
├── 02 wasm-pipeline ──┐
├── 03 datasets ───────┼── 05 playground ── 06 open-in-playground
└── 04 content ────────┘
07 deploy (after 02; last is fine)
```

02, 03, 04 can run in parallel after 01. 05 needs 02+03. 06 needs 05's URL
contract (already fixed above, but verify against the implementation). 07 last.

## Ticket status

Update this table as tickets complete.

| ticket | status |
|--------|--------|
| 01-scaffold | DONE |
| 02-wasm-pipeline | TODO |
| 03-datasets | TODO |
| 04-content | TODO |
| 05-playground | TODO |
| 06-open-in-playground | TODO |
| 07-deploy | TODO |

## Verification

- `cd site && npm ci && npm run docs:build` passes on every ticket.
- Playground: `make wasm site-wasm && cd site && npm run docs:dev`, open
  `/playground`, run `select tg_version();`, load the US states dataset and run
  the point-in-polygon query from the home page.
- `make test-wasm` still passes (wasm artifacts untouched).

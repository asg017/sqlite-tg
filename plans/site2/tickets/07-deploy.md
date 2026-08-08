# 07 — Deploy workflow

GitHub Pages deploy for the site, including the WASM build.

Depends on: 02 (make site-build). Do last.

## Reference

- sqlite-vec's `.github/workflows/site.yaml` (checkout → setup-node with npm
  cache on site/package-lock.json → npm ci → `make site-build` → Pages
  actions: configure-pages, upload-pages-artifact from `site/.vitepress/dist`,
  deploy-pages).
- sqlite-tg's `.github/workflows/test.yaml` — how this repo installs emsdk
  (pinned 5.0.4; commit 43c03a6 "pin CI emsdk to 5.0.4") and builds/caches the
  wasm target. Reuse the same steps/pins; if test.yaml caches the sqlite-src
  download or build dir, replicate that cache config.

## Tasks

1. `.github/workflows/site.yaml`:
   - `workflow_dispatch` + push to `main` with paths: `site/**`, `VERSION`,
     `reference.yaml`, `docs.md`, `.github/workflows/site.yaml` — plus the
     wasm inputs (`sqlite-tg.c`, `vendor/**`, `examples/wasm/**`, `Makefile`).
   - Job: checkout, emsdk 5.0.4 setup (copy from test.yaml), `make wasm`,
     setup-node w/ npm cache, `npm ci` in site/, `make site-build`, Pages
     permissions + environment + deploy steps mirroring sqlite-vec.
2. Do NOT enable Pages via API or push anything — workflow file only.
3. Sanity-check the workflow YAML (`actionlint` if available locally, else
   careful read / `python -c "import yaml,sys;yaml.safe_load(open(...))"`).

## Acceptance

- YAML parses; steps reference the same emsdk version/actions as test.yaml.
- `make site-build` locally (with existing dist/.wasm artifacts) produces
  `site/.vitepress/dist/` containing `playground/sqlite3.wasm` and
  `playground/data/datasets.json`.

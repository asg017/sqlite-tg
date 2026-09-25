# 02 — WASM → site pipeline

Get the sqlite-tg WASM build into the site's static assets, and add a
`site-build` make target the deploy workflow can call.

Depends on: 01.

## Background

- `make wasm` produces `dist/.wasm/sqlite3.mjs` and `dist/.wasm/sqlite3.wasm`
  (SQLite official WASM bundle with sqlite-tg statically linked). These already
  exist locally; do not rebuild them (rebuilding needs emsdk).
- The playground (ticket 05) will `import(/* @vite-ignore */ ...)` the `.mjs`
  from `<base>/playground/sqlite3.mjs` at runtime.

## Tasks

1. Makefile (put in a clearly-marked SITE section near the wasm section):
   - `site-wasm`: copies `dist/.wasm/sqlite3.mjs` + `sqlite3.wasm` into
     `site/public/playground/` (mkdir -p as needed; proper file dependencies on
     `$(TARGET_WASM_MJS)`/`$(TARGET_WASM_WASM)` so `make site-wasm` after
     `make wasm` is a no-op copy, but do NOT make `site-wasm` itself trigger an
     emsdk build if artifacts are missing — error clearly instead, e.g. a
     prerequisite check with a helpful message. Rationale: local contributors
     may not have emsdk; see memory note that local wasm builds need special
     setup).
   - `site-build`: `site-wasm` + `cd site && npm run docs:build`.
   - `site-dev`: `site-wasm` + `cd site && npm run docs:dev` (convenience).
2. `site/.gitignore`: add `public/playground/sqlite3.mjs` and
   `public/playground/sqlite3.wasm` (they are build artifacts, ~1-2 MB).
3. Add `site/public/playground/.gitkeep` (or ensure dir exists via make).

## Acceptance

- `make site-wasm` copies both files; `ls site/public/playground/` shows them.
- `rm -rf site/public/playground && make site-wasm` still works.
- With artifacts deleted from `dist/.wasm`, `make site-wasm` fails with a clear
  "run make wasm first (needs emscripten)" style message rather than invoking
  emcc.
- `make site-build` builds the site (after ticket 01).
- `git status` shows no untracked wasm artifacts.

# 01 — Clean VitePress scaffold

Rebuild `site/` as a proper sqlite-tg VitePress site modeled on
`/Users/alex/projects/sqlite-vec/site` (read that repo's `site/.vitepress/` for
reference — do NOT modify anything in sqlite-vec).

## Tasks

1. `site/package.json`:
   - deps: `vitepress@^1.6.4`, `vue@^3.5` (latest 3.x), keep
     `vitepress-plugin-tabs` (bump to `^0.9.1`), `@types/node` latest.
   - scripts: `docs:dev`, `docs:build`, `docs:preview` (as now).
   - Regenerate `package-lock.json` (`npm install`).
2. Rewrite `site/.vitepress/config.mts` for sqlite-tg (start from sqlite-vec's,
   it is more complete than sqlite-tg's current copy):
   - `PROJECT = "sqlite-tg"`, `title: "sqlite-tg"`,
     `description: "A SQLite extension for fast geospatial operations"`.
   - `base: "/sqlite-tg/"`, `lastUpdated: true`, `outline: "deep"`.
   - Read `VERSION` file like sqlite-vec does.
   - Keep the `sqlite.tmlanguage.json` markdown language registration AND the
     `vitepress-plugin-tabs` markdown plugin (both exist in current config).
   - nav: `API Reference` (/api-reference), **`Playground` (/playground)**,
     `♥ Sponsor` (https://github.com/sponsors/asg017), version dropdown with
     GitHub release + bindings links (Python/pypi, Node/npm, Ruby/gem,
     Datasette + sqlite-utils plugins — use `sqlite-tg` names; check
     `sqlite-dist.toml` / README for which bindings actually exist and only
     link those).
   - sidebar (placeholder links are fine; tickets 04/05 fill in pages):
     Getting Started (Introduction `/introduction`, Installation
     `/installation`), Documentation (API Reference `/api-reference`,
     Playground `/playground`), See also (sqlite-ecosystem, sqlite-vec GitHub
     links).
   - `search: { provider: "local" }`, `editLink` pattern for
     asg017/sqlite-tg, footer `MIT/Apache-2 License` + copyright Alex Garcia,
     socialLinks GitHub → https://github.com/asg017/sqlite-tg (current config
     points at vuejs/vitepress — fix).
   - head: shortcut icon + plausible analytics script with
     `data-domain: "alexgarcia.xyz/sqlite-tg"`.
3. `site/.vitepress/theme/`: port sqlite-vec's `style.css` brand treatment
   (Zilla Slab font-face for `.VPHero h1` / navbar title). Only copy the font
   CSS if the font files can be copied from sqlite-vec's `site/public/fonts/`
   into `site/public/fonts/`; otherwise keep the navbar/hero styling on a
   system serif stack. Keep `enhanceAppWithTabs` in `theme/index.ts`.
   Do NOT port the `<marquee>` WIP banner or Sponsors.vue.
4. Delete dead files: `site/index.html` (stale playground prototype).
5. Keep `site/index.md` working (it includes `installing.md.tmpl`); update its
   hero: tagline "A SQLite extension for fast geospatial operations", actions →
   Introduction, API Reference, Playground.
6. `site/.gitignore`: keep `.vitepress/cache`, add `.vitepress/dist`,
   `node_modules`.

## Acceptance

- `cd site && npm install && npm run docs:build` succeeds (dead-link failures
  for not-yet-written pages may be silenced with `ignoreDeadLinks: true` ONLY
  if needed — prefer creating stub pages for /introduction, /installation,
  /playground instead, since later tickets fill them).
- `npm ls vitepress` shows 1.6.4.
- No references to "sqlite-vec" remain in site config/theme except the
  "See also" link.

# 03 — Playground sample datasets

Sample geospatial databases for the playground, shipped as GeoJSON + a manifest,
loaded client-side.

Depends on: 01 (repo layout only — can run in parallel with 02).

## Tasks

1. Download and commit small, redistributable, public-domain datasets under
   `site/public/playground/data/`:
   - `us-states.geojson` — US states polygons, simplified (Census cartographic
     boundary 1:20m, converted to GeoJSON — a common source is
     https://raw.githubusercontent.com/PublicaMundi/MappingAPI/master/data/geojson/us-states.json
     or eric.clst.org's gz_2010_us_040_00_20m.json; keep under ~300 KB).
   - `world-countries.geojson` — Natural Earth 110m admin-0 countries (public
     domain; e.g. from https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson
     — strip properties down to a name field if the file is large; keep under
     ~500 KB).
   - `world-cities.geojson` — Natural Earth 110m populated places (points;
     name + population properties only).
   Verify each file: valid JSON, `type: "FeatureCollection"`, non-empty
   features, spot-check one geometry. Record source + license in a
   `site/public/playground/data/README.md`.
2. `site/public/playground/data/datasets.json` manifest — array of:
   ```json
   {
     "id": "us-states",
     "label": "US States (polygons)",
     "file": "us-states.geojson",
     "table": "states",
     "nameProperty": "NAME",
     "description": "US state boundaries (US Census 1:20m). Columns: name TEXT, geometry TEXT (GeoJSON)."
   }
   ```
   (Optionally an `extraProperties` map for e.g. city population → extra
   columns.) Keep the schema minimal but forward-compatible.
3. `site/.vitepress/theme/playground/datasets.ts` — a small client module:
   - `fetchDatasets(base: string): Promise<DatasetInfo[]>` (fetches manifest).
   - `loadDataset(db, dataset, base): Promise<void>` — fetches the GeoJSON,
     creates the table
     (`create table <table>(name text, ... , geometry text)`), inserts one row
     per feature with the feature's `geometry` object serialized via
     `JSON.stringify` (sqlite-tg accepts GeoJSON text directly). Use a prepared
     statement + a transaction. `db` is a `sqlite3.oo1.DB` — type it loosely
     (`any`).
   - No Vue imports here; pure logic so ticket 05 can consume it.
4. Do NOT wire any UI — that is ticket 05.

## Acceptance

- Files committed, each within size budget; `datasets.json` parses and every
  referenced file exists (add a tiny node check script or verify manually).
- `npm run docs:build` still passes (public/ files are copied verbatim;
  the unused .ts module must at least typecheck under the site build).
- README.md documents provenance/licenses.

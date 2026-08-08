# Playground sample datasets

Small GeoJSON datasets loaded by the sqlite-tg playground
(`site/.vitepress/theme/playground/datasets.ts` reads `datasets.json` and
inserts one row per feature). Properties were slimmed down and coordinates
rounded (3–4 decimal places) to keep file sizes small; see
`plans/site2/tickets/03-datasets.md` for the processing details.

## Files

| file | contents | features | source | license |
|------|----------|----------|--------|---------|
| `us-states.geojson` | US state boundaries (polygons), incl. DC and Puerto Rico | 52 | [PublicaMundi/MappingAPI `us-states.json`](https://raw.githubusercontent.com/PublicaMundi/MappingAPI/master/data/geojson/us-states.json), derived from US Census Bureau cartographic boundary files (1:20m, simplified) | US Government work, public domain |
| `world-countries.geojson` | Country boundaries (admin-0 polygons) | 177 | [Natural Earth 1:110m admin-0 countries](https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson) | [Public domain](https://www.naturalearthdata.com/about/terms-of-use/) |
| `world-cities.geojson` | Populated places (points) with population | 243 | [Natural Earth 1:110m populated places (simple)](https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_populated_places_simple.geojson) | [Public domain](https://www.naturalearthdata.com/about/terms-of-use/) |

Retrieved 2026-08-08.

## Processing

Each file was reduced to a bare `FeatureCollection` whose features carry only
the properties used by the playground tables:

- `us-states.geojson`: `name`; coordinates rounded to 4 decimals.
- `world-countries.geojson`: `name` (from `NAME`), `continent` (from
  `CONTINENT`); coordinates rounded to 3 decimals (~110 m, well below the
  1:110m source resolution).
- `world-cities.geojson`: `name`, `population` (from `pop_max`); coordinates
  rounded to 4 decimals.

## Manifest

`datasets.json` describes each dataset for the playground UI: `id`, `label`,
`file`, `table` (SQL table name), `nameProperty` (GeoJSON property used for the
`name` column), optional `extraProperties` (GeoJSON property → SQLite column
type for extra columns), `description`, and optional `exampleSql` (pre-fills
the playground editor).

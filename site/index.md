---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "sqlite-tg"
  tagline: A SQLite extension for fast geospatial operations
  actions:
    - theme: brand
      text: Introduction
      link: /introduction
    - theme: alt
      text: API Reference
      link: /api-reference
    - theme: alt
      text: Playground
      link: /playground

features:
  - title: Fast point-in-polygon
    details: Quick geometry intersection and containment checks, with optional indexing, thanks to the tg geometry library
  - title: WKT, WKB, and GeoJSON
    details: Parse, convert between, and generate all three common geometry formats with plain SQL functions
  - title: Runs anywhere
    details: A small dependency-free SQLite extension for laptops, servers, mobile devices, and browsers with WASM
---

<div style="margin: auto; max-width: 640px;">

```sqlite
-- Find the US state a latitude/longitude point lies in
select name
from states
where tg_intersects(
  geometry,
  tg_point(-111.023333, 35.028056)
);
/*
┌─────────┐
│ name    │
├─────────┤
│ Arizona │
└─────────┘
*/
```

</div>

## Installing
<!--@include: installing.md.tmpl -->

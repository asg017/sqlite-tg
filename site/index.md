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
---

<div style="margin: auto; max-width: 640px;">

```sqlite
-- Find the US state a latitude/longitude point lies in
select 
  geometry ->> '$[0].properties.NAME' as name
from tg_each(readfile('us-states.geojson'))
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

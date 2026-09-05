# 06 — Docs, reference, examples

## Tasks

1. `reference.yaml` tg0 entry: KNN section — the three query shapes,
   `metric=` option, `distance` column semantics, "always ORDER BY
   distance + LIMIT", limitations (point query only, planar edges,
   antimeridian-straddling geometries, sphere not ellipsoid). Regenerate
   with `make docs` / `make docs-check`.
2. `docs.md` prose: a "Nearest-neighbor queries" section with the
   correlated-subquery "nearest X for each Y" idiom and the bbox/radius
   idioms.
3. site2 port: when `asg017/site2` has merged, port the entry into
   `site/build-ref.mjs` format (already a pending follow-up for the tg0
   entry itself) and add a playground example on the `world-cities`
   dataset ("10 cities nearest to where I clicked" reads well).
4. Examples: extend `examples/overture` (places) or `examples/la-buildings`
   with a KNN query file + timing note; the LA buildings set doubles as the
   polygon-candidate benchmark.
5. NOTES checkbox; tg0-upgrade CHANGELOG snippet gains a KNN line.

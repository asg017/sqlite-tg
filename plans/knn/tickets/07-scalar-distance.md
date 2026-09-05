# 07 — Optional: `tg_distance_sphere(a, b)` scalar

Only if open question 3 is answered yes. Depends on 01 alone, so it can also
be the vehicle for 01's tests (land it first in that case).

## Tasks

1. `tg_distance_sphere(a, b)`: at least one argument must be a Point (error
   naming both types otherwise); returns meters via
   `tg_geom_point_distance(TG_METRIC_GEODESIC, ...)`; NULL if either input
   is NULL; empty geometry → NULL. Validates lon/lat ranges (error).
2. No planar counterpart (api-design memory: planar measurements are out).
3. `reference.yaml` entry with the LA↔NYC example; snapshot tests
   incl. polygon containing the point → 0.

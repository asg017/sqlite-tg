# 06 — Polish: xRename, error paths, OOM audit

## Status: complete (a6a8315)

Ride-along fixes not covered by earlier tickets (decision 15).

## Tasks

1. `xRename`: rename all shadow tables (`info`, `content`, `rowids`,
   `rtree%02d` — renaming the rtree vtab moves its own shadows). Test
   `ALTER TABLE ... RENAME` end-to-end including post-rename queries and
   writes.
2. OOM/error audit of the new SQL-string builders (the old code had three
   "TODO what needs to be freed here" sites — sqlite-tg.c:2903, :2948,
   :3176; don't recreate them).
3. Confirm every error path uses the shared `tg0_set_error` helper and no
   path returns SQLITE_OK with an error message set.
4. Consider `iVersion = 4` + `xIntegrity` (check content↔rtree 1:1
   invariant, decision 11) — implement if cheap, else file a NOTES entry.
5. Sweep NOTES: check off the tg0 items this workstream completed
   (managed-rtree predicate list, geom idx reachability), add entries for
   deferred work (KNN follow-up, partition keys, parsed-geometry cache).

## Acceptance

- Rename test (with data, all pk flavors) passes; queries/writes work after
  rename; `sqlite_master` snapshot post-rename.
- `tests/test-valgrind.sh` and `make test-memory` clean across the full new
  test suite.
- Fault-injection pass if practical (SQLITE_FAULTINJECTOR not in this repo —
  a manual review checklist in the PR is acceptable).

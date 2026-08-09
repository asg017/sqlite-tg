# 05 — Data-column constraint pushdown (optimization; may slip)

Fold cheap comparisons on plain data columns into the content-table query so
they run before WKB parse + exact predicate (decision 10). Pure performance —
correctness never depends on this ticket; if it slips, SQLite post-filters.

## Tasks

1. `xBestIndex`: additionally consume EQ/NE/LT/LE/GT/GE constraints on data
   columns during predicate + fullscan plans. idxStr `'&'` blocks:
   `['&']['A'+data-col][op]['_']` (operator alphabet mirroring vec0,
   sqlite-vec.c:5993). Set `omit=1` only when semantics are exactly SQLite's
   (beware: SQLite comparison affinity vs. our verbatim storage — when in
   doubt set omit=0 and let SQLite double-check).
2. `xFilter`: append `AND data%02d <op> ?` clauses to the content
   SELECT and bind argv values.
3. Cost model: lower estimatedCost when constraints are consumed so the
   planner prefers the vtab doing the filtering.

## Acceptance

- Results identical with and without pushdown on a mixed fixture (property
  test: same query via `tg_to_wkt()` wrapper to defeat interception).
- `EXPLAIN QUERY PLAN` / idxStr snapshot showing consumed constraints.
- Micro-benchmark note in the PR (candidates skipped before parse), even if
  informal.

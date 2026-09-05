# 04 — Pruning: same-column predicate folding, distance max, pushdown, LIMIT

Turns "closest places within this bbox / within 5 km" from correct-but-slow
into indexed. Decisions 10, 11, 12.

## Tasks

1. **Predicate folding.** In xBestIndex, when plan `'4'` is chosen and a tg
   predicate constraint targets the *same* geometry column, consume it too
   (`argvIndex = 2`, `omit = 1`) and append a `'P'` block reusing the plan
   `'3'` alphabet. Only one; extra predicates stay unconsumed. Predicates on
   other geometry columns stay unconsumed.
2. In the pop loop, prune cells by strategy before pushing (overlap /
   within / contains → bbox tests against the query rect with the same
   float32 outward rounding as plan `'3'`; disjoint → none) and apply the
   exact predicate to the parsed candidate before pushing `ROW`.
3. **Distance max.** Consume `distance < ?` / `distance <= ?` (`omit = 0`)
   as a `'D'` block; in xFilter read the bound (must be numeric, else
   ignore the prune and let SQLite filter); stop the traversal when the heap
   minimum key exceeds it (strict vs non-strict per op).
4. **Pushdown** in the candidate fetch: reuse the existing `'&'` machinery
   to append clauses to the per-rowid SELECT; a rejected candidate is
   skipped before parse. Same affinity/collation rules as the existing code
   (share the helper, do not copy it).
5. **LIMIT hint**: already used for `estimatedRows` in 03; nothing else —
   document why `k` is unnecessary.
6. Tests: bbox-restricted KNN equals `WHERE tg_intersects(...)` post-filter
   results (property test over the fixture); `distance <= r` equals the
   unpruned query filtered in Python; pushdown on/off equivalence; idxStr
   snapshots for each combination; a node-visit counter assertion showing
   the bbox prune actually prunes.

## Acceptance

- "10 nearest inside a small bbox on the other side of the world" visits
  O(log n) nodes instead of the whole tree (counter test).

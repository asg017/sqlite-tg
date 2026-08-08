<script setup lang="ts">
// One executed statement: a result table for row-producing statements, or a
// compact "OK" line for the rest. BLOBs render as X'..' hex, NULL as italic.
import { computed } from "vue";
import type { SqlValue, StatementResult } from "./sqlite";

const props = defineProps<{ result: StatementResult }>();

const MAX_BLOB_BYTES = 48;

function blobHex(bytes: Uint8Array): string {
  let hex = "";
  const n = Math.min(bytes.length, MAX_BLOB_BYTES);
  for (let i = 0; i < n; i++) hex += bytes[i].toString(16).padStart(2, "0");
  const ellipsis = bytes.length > MAX_BLOB_BYTES ? "…" : "";
  return `X'${hex}'${ellipsis}`;
}

type Cell = { kind: "null" | "blob" | "value"; text: string };

function toCell(value: SqlValue): Cell {
  if (value === null) return { kind: "null", text: "NULL" };
  if (value instanceof Uint8Array) return { kind: "blob", text: blobHex(value) };
  return { kind: "value", text: String(value) };
}

const cells = computed<Cell[][]>(() =>
  props.result.rows.map((row) => row.map(toCell))
);

const summary = computed(() => {
  const r = props.result;
  if (r.columns.length > 0) {
    const rows = `${r.rows.length} row${r.rows.length === 1 ? "" : "s"}`;
    return r.truncated ? `first ${rows} (truncated)` : rows;
  }
  if (r.changes && r.changes > 0) {
    return `OK, ${r.changes} row${r.changes === 1 ? "" : "s"} changed`;
  }
  return "OK";
});
</script>

<template>
  <section class="pg-result">
    <div class="pg-result-head">
      <code class="pg-result-sql">{{ result.sql }}</code>
      <span class="pg-result-summary">{{ summary }}</span>
    </div>
    <div v-if="result.columns.length > 0" class="pg-result-scroll">
      <table class="pg-table">
        <thead>
          <tr>
            <th v-for="(col, i) in result.columns" :key="i">{{ col }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(row, ri) in cells" :key="ri">
            <td v-for="(cell, ci) in row" :key="ci">
              <em v-if="cell.kind === 'null'" class="pg-null">NULL</em>
              <code v-else-if="cell.kind === 'blob'" class="pg-blob">{{
                cell.text
              }}</code>
              <span v-else>{{ cell.text }}</span>
            </td>
          </tr>
          <tr v-if="cells.length === 0">
            <td class="pg-empty" :colspan="result.columns.length">no rows</td>
          </tr>
        </tbody>
      </table>
    </div>
    <p v-if="result.truncated" class="pg-truncated">
      Output truncated to the first {{ result.rows.length }} rows.
    </p>
  </section>
</template>

<style scoped>
.pg-result {
  margin-top: 20px;
}
.pg-result-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 6px;
}
.pg-result-sql {
  font-family: var(--vp-font-family-mono);
  font-size: 12px;
  color: var(--vp-c-text-2);
  background: transparent;
  padding: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.pg-result-summary {
  flex: none;
  font-size: 12px;
  color: var(--vp-c-text-3);
}
.pg-result-scroll {
  overflow-x: auto;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
}
.pg-table {
  display: table;
  width: 100%;
  margin: 0;
  border-collapse: collapse;
  font-size: 13px;
}
.pg-table th,
.pg-table td {
  border: none;
  border-bottom: 1px solid var(--vp-c-divider);
  padding: 6px 12px;
  text-align: left;
  white-space: nowrap;
  max-width: 28rem;
  overflow: hidden;
  text-overflow: ellipsis;
}
.pg-table thead th {
  background-color: var(--vp-c-bg-soft);
  color: var(--vp-c-text-2);
  font-weight: 600;
}
.pg-table tbody tr:last-child td {
  border-bottom: none;
}
.pg-table tbody tr:hover td {
  background-color: var(--vp-c-bg-soft);
}
.pg-null {
  color: var(--vp-c-text-3);
  font-style: italic;
}
.pg-blob {
  font-family: var(--vp-font-family-mono);
  font-size: 12px;
  color: var(--vp-c-purple-1);
  background: transparent;
  padding: 0;
}
.pg-empty {
  color: var(--vp-c-text-3);
  font-style: italic;
}
.pg-truncated {
  margin: 6px 0 0;
  font-size: 12px;
  color: var(--vp-c-yellow-1);
}
</style>

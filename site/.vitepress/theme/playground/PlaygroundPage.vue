<script setup lang="ts">
// The sqlite-tg playground: toolbar (run / dataset picker / share / status),
// CodeMirror SQL editor, and per-statement results. Client-only — the page
// mounts this inside <ClientOnly> via defineAsyncComponent.
import { computed, onBeforeUnmount, onMounted, ref, shallowRef } from "vue";
import { withBase } from "vitepress";
import SqlEditor from "./SqlEditor.vue";
import ResultTable from "./ResultTable.vue";
import { execAll, initSqlite, type ExecOutcome } from "./sqlite";
import { fetchDatasets, loadDataset, type DatasetInfo } from "./datasets";
import { decodeSql, playgroundUrl } from "./urlState";

const DEFAULT_SQL = "select tg_version();";

type Phase = "loading" | "ready" | "init-error";

const phase = ref<Phase>("loading");
const initError = ref("");
const tgVersion = ref("");
const sqliteVersion = ref("");

const datasets = ref<DatasetInfo[]>([]);
const selectedId = ref("");
const datasetBusy = ref(false);
const datasetError = ref("");

interface SchemaSummary {
  table: string;
  columns: { name: string; type: string }[];
  rowCount: number;
}
const schema = ref<SchemaSummary | null>(null);

const editorSql = ref(DEFAULT_SQL);
const outcome = shallowRef<ExecOutcome | null>(null);
const elapsedMs = ref<number | null>(null);
const running = ref(false);
const copied = ref(false);

const sqlite3 = shallowRef<any>(null);
const db = shallowRef<any>(null);

const base = withBase("/");
const ready = computed(() => phase.value === "ready");
const selectedDataset = computed(
  () => datasets.value.find((d) => d.id === selectedId.value) ?? null
);
const elapsedText = computed(() => {
  if (elapsedMs.value === null) return "";
  return elapsedMs.value < 1 ? "<1 ms" : `${Math.round(elapsedMs.value)} ms`;
});
const schemaText = computed(() => {
  const s = schema.value;
  if (!s) return "";
  const cols = s.columns.map((c) => `${c.name} ${c.type}`.trim()).join(", ");
  return `${s.table}(${cols}) — ${s.rowCount} rows`;
});

function quoteIdent(name: string): string {
  return `"${name.replaceAll('"', '""')}"`;
}

function resetDb() {
  if (db.value) db.value.close();
  db.value = new sqlite3.value.oo1.DB(":memory:");
}

async function applyDataset(id: string, prefill: boolean): Promise<boolean> {
  selectedId.value = id;
  datasetError.value = "";
  outcome.value = null;
  elapsedMs.value = null;
  schema.value = null;
  resetDb();
  const ds = datasets.value.find((d) => d.id === id);
  if (!ds) return id === "";
  datasetBusy.value = true;
  try {
    await loadDataset(db.value, ds, base);
    const table = ds.table;
    const columns = db.value
      .exec({
        sql: "select name, type from pragma_table_info(?)",
        bind: [table],
        returnValue: "resultRows",
      })
      .map(([name, type]: [string, string]) => ({ name, type }));
    const rowCount = db.value.selectValue(
      `select count(*) from ${quoteIdent(table)}`
    ) as number;
    schema.value = { table, columns, rowCount };
    if (prefill && ds.exampleSql) editorSql.value = ds.exampleSql;
    return true;
  } catch (error) {
    datasetError.value = `Failed to load dataset "${ds.label}": ${
      error instanceof Error ? error.message : String(error)
    }`;
    return false;
  } finally {
    datasetBusy.value = false;
  }
}

function onDatasetChange(event: Event) {
  applyDataset((event.target as HTMLSelectElement).value, true);
}

function run() {
  if (!ready.value || running.value || datasetBusy.value) return;
  running.value = true;
  const t0 = performance.now();
  try {
    outcome.value = execAll(sqlite3.value, db.value, editorSql.value);
  } catch (error) {
    outcome.value = {
      results: [],
      error: {
        message: error instanceof Error ? error.message : String(error),
        sql: "",
      },
    };
  } finally {
    elapsedMs.value = performance.now() - t0;
    running.value = false;
  }
}

async function share() {
  const url = playgroundUrl(
    location.origin + withBase("/playground.html"),
    editorSql.value,
    selectedId.value || null
  );
  try {
    await navigator.clipboard.writeText(url);
    copied.value = true;
    setTimeout(() => (copied.value = false), 1500);
  } catch {
    // Clipboard API unavailable (e.g. non-secure context): show the URL.
    window.prompt("Copy this playground URL:", url);
  }
}

onMounted(async () => {
  const params = new URLSearchParams(location.search);
  const sqlParam = params.get("sql");
  const dbParam = params.get("db");
  const decoded = sqlParam ? decodeSql(sqlParam) : null;

  try {
    sqlite3.value = await initSqlite(base);
  } catch (error) {
    phase.value = "init-error";
    initError.value = error instanceof Error ? error.message : String(error);
    return;
  }
  resetDb();
  tgVersion.value = db.value.selectValue("select tg_version()") as string;
  sqliteVersion.value = sqlite3.value.version?.libVersion ?? "";

  try {
    datasets.value = await fetchDatasets(base);
  } catch (error) {
    datasetError.value = `Failed to load the dataset list: ${
      error instanceof Error ? error.message : String(error)
    }`;
  }
  phase.value = "ready";

  // URL contract: ?sql=<base64url> pre-fills + auto-runs; &db=<id> selects
  // and loads a dataset first.
  let loaded = true;
  if (dbParam && datasets.value.some((d) => d.id === dbParam)) {
    loaded = await applyDataset(dbParam, !decoded);
  }
  if (decoded !== null) {
    editorSql.value = decoded;
    if (loaded) run();
  }
});

onBeforeUnmount(() => {
  if (db.value) {
    db.value.close();
    db.value = null;
  }
});
</script>

<template>
  <div class="pg">
    <div class="pg-header">
      <h1>Playground</h1>
      <p class="pg-tagline">
        Run <code>sqlite-tg</code> SQL directly in your browser — everything
        stays client-side, powered by WebAssembly.
      </p>
    </div>

    <div v-if="phase === 'loading'" class="pg-loading">Loading SQLite WASM…</div>

    <div v-else-if="phase === 'init-error'" class="pg-error">
      <strong>Failed to load the SQLite WASM module.</strong>
      <p>{{ initError }}</p>
      <p>
        If you are running the site locally, build and copy the WASM artifacts
        first: <code>make wasm site-wasm</code>, then reload this page.
      </p>
    </div>

    <template v-else>
      <div class="pg-toolbar">
        <button
          class="pg-btn pg-btn-run"
          :disabled="running || datasetBusy"
          title="Run (Cmd/Ctrl+Enter)"
          @click="run"
        >
          ▶ Run
        </button>
        <label class="pg-dataset">
          <span class="pg-dataset-label">Dataset</span>
          <select
            class="pg-select"
            :value="selectedId"
            :disabled="datasetBusy"
            @change="onDatasetChange"
          >
            <option value="">(empty database)</option>
            <option v-for="ds in datasets" :key="ds.id" :value="ds.id">
              {{ ds.label }}
            </option>
          </select>
        </label>
        <button class="pg-btn" @click="share">
          {{ copied ? "✓ Copied!" : "Share" }}
        </button>
        <span class="pg-status">
          <template v-if="datasetBusy">Loading dataset…</template>
          <template v-else-if="running">Running…</template>
          <template v-else-if="elapsedMs !== null">{{ elapsedText }}</template>
          <template v-else>
            tg {{ tgVersion }} · SQLite {{ sqliteVersion }}
          </template>
        </span>
      </div>

      <div v-if="selectedDataset && schema" class="pg-schema">
        <p class="pg-schema-line">
          <code>{{ schemaText }}</code>
        </p>
        <p v-if="selectedDataset.description" class="pg-schema-desc">
          {{ selectedDataset.description }}
        </p>
      </div>
      <div v-if="datasetError" class="pg-error">{{ datasetError }}</div>

      <SqlEditor v-model="editorSql" @run="run" />
      <p class="pg-hint">
        Press <kbd>Cmd/Ctrl+Enter</kbd> to run.
      </p>

      <div v-if="outcome" class="pg-results">
        <ResultTable
          v-for="(result, i) in outcome.results"
          :key="i"
          :result="result"
        />
        <div v-if="outcome.error" class="pg-error">
          <strong>SQL error:</strong> {{ outcome.error.message }}
          <pre v-if="outcome.error.sql" class="pg-error-sql">{{
            outcome.error.sql
          }}</pre>
        </div>
        <p
          v-if="!outcome.error && outcome.results.length === 0"
          class="pg-noresults"
        >
          Nothing to run.
        </p>
      </div>
    </template>
  </div>
</template>

<style scoped>
.pg {
  max-width: 1104px;
  margin: 0 auto;
  padding: 32px 24px 96px;
}
.pg-header h1 {
  font-size: 28px;
  font-weight: 700;
  letter-spacing: -0.02em;
  margin: 0 0 4px;
}
.pg-tagline {
  color: var(--vp-c-text-2);
  margin: 0 0 24px;
}
.pg-loading {
  color: var(--vp-c-text-2);
  padding: 32px 0;
}
.pg-toolbar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  margin-bottom: 12px;
}
.pg-btn {
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  padding: 5px 14px;
  font-size: 14px;
  font-weight: 500;
  color: var(--vp-c-text-1);
  background-color: var(--vp-c-bg-soft);
  transition: border-color 0.2s, background-color 0.2s;
}
.pg-btn:hover:not(:disabled) {
  border-color: var(--vp-c-brand-1);
}
.pg-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.pg-btn-run {
  color: var(--vp-c-white);
  background-color: var(--vp-c-brand-3);
  border-color: var(--vp-c-brand-3);
}
.pg-btn-run:hover:not(:disabled) {
  background-color: var(--vp-c-brand-2);
  border-color: var(--vp-c-brand-2);
}
.pg-dataset {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.pg-dataset-label {
  font-size: 13px;
  color: var(--vp-c-text-2);
}
.pg-select {
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  padding: 5px 10px;
  font-size: 14px;
  color: var(--vp-c-text-1);
  background-color: var(--vp-c-bg-soft);
}
.pg-status {
  margin-left: auto;
  font-size: 13px;
  color: var(--vp-c-text-3);
  font-family: var(--vp-font-family-mono);
}
.pg-schema {
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  background-color: var(--vp-c-bg-soft);
  padding: 10px 14px;
  margin-bottom: 12px;
}
.pg-schema-line {
  margin: 0;
}
.pg-schema-line code {
  background: transparent;
  padding: 0;
  font-size: 13px;
  color: var(--vp-c-text-1);
}
.pg-schema-desc {
  margin: 4px 0 0;
  font-size: 13px;
  color: var(--vp-c-text-2);
}
.pg-hint {
  margin: 6px 0 0;
  font-size: 12px;
  color: var(--vp-c-text-3);
}
.pg-hint kbd {
  font-family: var(--vp-font-family-mono);
  border: 1px solid var(--vp-c-divider);
  border-bottom-width: 2px;
  border-radius: 4px;
  padding: 1px 5px;
  font-size: 11px;
}
.pg-results {
  margin-top: 8px;
}
.pg-error {
  margin-top: 16px;
  border: 1px solid var(--vp-c-red-2);
  border-radius: 8px;
  background-color: var(--vp-c-red-soft);
  color: var(--vp-c-text-1);
  padding: 12px 16px;
  font-size: 14px;
}
.pg-error p {
  margin: 8px 0 0;
}
.pg-error-sql {
  margin: 8px 0 0;
  padding: 8px 10px;
  border-radius: 6px;
  background-color: var(--vp-c-bg-soft);
  font-family: var(--vp-font-family-mono);
  font-size: 12px;
  overflow-x: auto;
  white-space: pre-wrap;
  max-height: 8rem;
}
.pg-noresults {
  color: var(--vp-c-text-3);
  font-style: italic;
}
</style>

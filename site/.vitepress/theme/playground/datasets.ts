// Sample-dataset loading for the sqlite-tg playground.
//
// Pure logic — no Vue imports — so it can be consumed by the playground
// component (ticket 05) and tested independently. Datasets live under
// `<base>playground/data/`: a `datasets.json` manifest plus one GeoJSON
// FeatureCollection per dataset (see site/public/playground/data/README.md).

/** One entry in the `datasets.json` manifest. */
export interface DatasetInfo {
  /** Stable identifier, used in `?db=<id>` playground URLs. */
  id: string;
  /** Human-readable label for the dataset picker. */
  label: string;
  /** GeoJSON filename, relative to `<base>playground/data/`. */
  file: string;
  /** SQL table name the dataset is loaded into. */
  table: string;
  /** GeoJSON feature property used for the table's `name` column. */
  nameProperty: string;
  /**
   * Optional extra columns: GeoJSON property name -> SQLite column type
   * (e.g. `{ "population": "INTEGER" }`). The column is named after the
   * property.
   */
  extraProperties?: Record<string, string>;
  /** Short description shown in the UI (mentions the table schema). */
  description?: string;
  /** Optional example query to pre-fill the editor with. */
  exampleSql?: string;
}

interface GeoJSONFeature {
  type: "Feature";
  properties: Record<string, unknown> | null;
  geometry: { type: string; coordinates: unknown } | null;
}

interface GeoJSONFeatureCollection {
  type: "FeatureCollection";
  features: GeoJSONFeature[];
}

function dataUrl(base: string, file: string): string {
  const b = base.endsWith("/") ? base : `${base}/`;
  return `${b}playground/data/${file}`;
}

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`Failed to fetch ${url}: ${res.status} ${res.statusText}`);
  }
  return (await res.json()) as T;
}

/**
 * Fetch the dataset manifest.
 *
 * @param base site base path (VitePress `withBase("/")`, e.g. `/sqlite-tg/`)
 */
export async function fetchDatasets(base: string): Promise<DatasetInfo[]> {
  const datasets = await fetchJson<DatasetInfo[]>(dataUrl(base, "datasets.json"));
  if (!Array.isArray(datasets)) {
    throw new Error("datasets.json: expected an array of datasets");
  }
  return datasets;
}

// Quote an SQL identifier ("" doubles any embedded quote).
function quoteIdent(name: string): string {
  return `"${name.replaceAll('"', '""')}"`;
}

/**
 * Fetch a dataset's GeoJSON and load it into `db` as table `dataset.table`
 * with columns `name TEXT`, one column per `extraProperties` entry, and
 * `geometry TEXT` holding each feature's GeoJSON geometry — directly usable
 * by every `tg_*` function. Drops any existing table of the same name, and
 * inserts inside a transaction with a prepared statement.
 *
 * @param db an open `sqlite3.oo1.DB` (typed loosely to avoid a dependency on
 *   the sqlite3 WASM types)
 */
export async function loadDataset(
  db: any,
  dataset: DatasetInfo,
  base: string
): Promise<void> {
  const geojson = await fetchJson<GeoJSONFeatureCollection>(
    dataUrl(base, dataset.file)
  );
  if (geojson.type !== "FeatureCollection" || !Array.isArray(geojson.features)) {
    throw new Error(`${dataset.file}: expected a GeoJSON FeatureCollection`);
  }

  const table = quoteIdent(dataset.table);
  const extras = Object.entries(dataset.extraProperties ?? {});
  const columns = [
    "name TEXT",
    ...extras.map(([prop, type]) => `${quoteIdent(prop)} ${type}`),
    "geometry TEXT",
  ];
  const placeholders = new Array(columns.length).fill("?").join(", ");

  db.exec(`DROP TABLE IF EXISTS ${table}`);
  db.exec(`CREATE TABLE ${table}(${columns.join(", ")})`);

  const stmt = db.prepare(`INSERT INTO ${table} VALUES (${placeholders})`);
  db.exec("BEGIN");
  try {
    for (const feature of geojson.features) {
      if (!feature.geometry) continue;
      const props = feature.properties ?? {};
      stmt.bind([
        (props[dataset.nameProperty] as string | null) ?? null,
        ...extras.map(([prop]) => (props[prop] as string | number | null) ?? null),
        JSON.stringify(feature.geometry),
      ]);
      stmt.stepReset();
    }
    db.exec("COMMIT");
  } catch (error) {
    db.exec("ROLLBACK");
    throw error;
  } finally {
    stmt.finalize();
  }
}

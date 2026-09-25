// SQLite WASM bootstrap + multi-statement SQL executor for the playground.
//
// Pure logic — no Vue imports. The sqlite3 module is loaded at runtime from
// `<base>playground/sqlite3.mjs` (copied there by `make site-wasm`), so this
// file never statically imports the WASM bundle and stays SSR-safe.

/** A single SQL value as converted to JS by sqlite3_column_js(). */
export type SqlValue = null | number | bigint | string | Uint8Array;

/** The outcome of one successfully-executed SQL statement. */
export interface StatementResult {
  /** The statement's SQL text (trimmed). */
  sql: string;
  /** Result column names; empty for statements that return no rows. */
  columns: string[];
  /** Result rows (capped at the `maxRows` passed to execAll). */
  rows: SqlValue[][];
  /** True when rows were dropped because the statement produced > maxRows. */
  truncated: boolean;
  /**
   * Rows inserted/updated/deleted by this statement
   * (sqlite3_total_changes delta), or null for pure queries.
   */
  changes: number | null;
}

export interface ExecError {
  message: string;
  /** SQL of the failing statement (or remaining input on a parse error). */
  sql: string;
}

export interface ExecOutcome {
  /** Results of the statements that ran before any error. */
  results: StatementResult[];
  /** Set when execution stopped early; statements after it did not run. */
  error: ExecError | null;
}

/** Default cap on rendered rows per statement. */
export const MAX_ROWS = 1000;

let initPromise: Promise<any> | null = null;

// A dynamic import() that Vite cannot see. `/* @vite-ignore */ import(url)`
// is not enough: Vite's dev-server import analysis still rewrites it to
// `__vite__injectQuery(url, 'import')`, and public-dir files 500 when
// requested with `?import`. Routing the import through Function() keeps both
// dev and build from touching it, so the browser natively imports the file
// straight from `public/`.
const nativeImport = new Function(
  "url",
  "return import(url)"
) as (url: string) => Promise<any>;

/**
 * Load and initialize the sqlite3 WASM module from
 * `<base>playground/sqlite3.mjs`. Memoized: repeat calls share one module.
 *
 * @param base site base path (VitePress `withBase("/")`, e.g. `/sqlite-tg/`)
 * @returns the sqlite3 namespace object
 */
export function initSqlite(base: string): Promise<any> {
  if (!initPromise) {
    const b = base.endsWith("/") ? base : `${base}/`;
    const url = `${b}playground/sqlite3.mjs`;
    initPromise = nativeImport(url).then((mod) =>
      mod.default({
        // Keep the module's stdout/stderr chatter out of the console.
        print: () => {},
        printErr: () => {},
      })
    );
    initPromise.catch(() => {
      // Allow a retry (e.g. after `make site-wasm`) instead of caching failure.
      initPromise = null;
    });
  }
  return initPromise;
}

/**
 * Execute every statement in `sql` against `db`, returning one
 * StatementResult per non-empty statement.
 *
 * Uses the C-API prepare loop (sqlite3_prepare_v3 + pzTail) — the same
 * pattern as oo1 DB.exec()'s internals — so each statement, including each
 * SELECT, gets its own result set. Statements are executed in order;
 * execution stops at the first error and the outcome carries both the
 * partial results and the error.
 *
 * @param sqlite3 the sqlite3 namespace from initSqlite()
 * @param db an open `sqlite3.oo1.DB`
 */
export function execAll(
  sqlite3: any,
  db: any,
  sql: string,
  maxRows: number = MAX_ROWS
): ExecOutcome {
  const capi = sqlite3.capi;
  const wasm = sqlite3.wasm;
  const ptrSize: number = wasm.ptr?.size ?? 4;
  const results: StatementResult[] = [];
  const stack = wasm.scopedAllocPush();
  let pStmt = 0;
  try {
    let sqlByteLen: number = wasm.jstrlen(sql);
    // One allocation: [ppStmt][pzTail][SQL bytes + NUL].
    const ppStmt = wasm.scopedAlloc(2 * ptrSize + sqlByteLen + 1);
    const pzTail = ppStmt + ptrSize;
    let pSql = pzTail + ptrSize;
    const pSqlEnd = pSql + sqlByteLen;
    wasm.jstrcpy(sql, wasm.heap8(), pSql, sqlByteLen, false);
    wasm.poke8(pSqlEnd, 0 /* NUL terminator */);

    while (pSql && wasm.peek8(pSql)) {
      pStmt = 0;
      wasm.pokePtr([ppStmt, pzTail], 0);
      const zHead = pSql;
      const rc = capi.sqlite3_prepare_v3(
        db.pointer,
        pSql,
        sqlByteLen,
        0,
        ppStmt,
        pzTail
      );
      if (rc !== 0) {
        return {
          results,
          error: {
            message: capi.sqlite3_errmsg(db.pointer),
            sql: wasm.cstrToJs(zHead).trim(),
          },
        };
      }
      [pStmt, pSql] = wasm.peekPtr([ppStmt, pzTail]) as [number, number];
      sqlByteLen = pSqlEnd - pSql;
      if (!pStmt) continue; // whitespace / comment-only tail

      const stmtSql: string = (capi.sqlite3_sql(pStmt) ?? "").trim();
      const colCount: number = capi.sqlite3_column_count(pStmt);
      const columns: string[] = [];
      for (let i = 0; i < colCount; i++) {
        columns.push(capi.sqlite3_column_name(pStmt, i));
      }

      const changesBefore: number = capi.sqlite3_total_changes(db.pointer);
      const rows: SqlValue[][] = [];
      let truncated = false;
      let rcStep: number;
      while ((rcStep = capi.sqlite3_step(pStmt)) === capi.SQLITE_ROW) {
        if (rows.length >= maxRows) {
          // Keep stepping (so side effects like INSERT...RETURNING finish)
          // but stop collecting rows.
          truncated = true;
          continue;
        }
        const row: SqlValue[] = [];
        for (let i = 0; i < colCount; i++) {
          row.push(capi.sqlite3_column_js(pStmt, i));
        }
        rows.push(row);
      }
      if (rcStep !== capi.SQLITE_DONE) {
        const message: string = capi.sqlite3_errmsg(db.pointer);
        capi.sqlite3_finalize(pStmt);
        pStmt = 0;
        return { results, error: { message, sql: stmtSql } };
      }
      const rcFin: number = capi.sqlite3_finalize(pStmt);
      pStmt = 0;
      if (rcFin !== 0) {
        return {
          results,
          error: { message: capi.sqlite3_errmsg(db.pointer), sql: stmtSql },
        };
      }

      const changes = capi.sqlite3_total_changes(db.pointer) - changesBefore;
      results.push({
        sql: stmtSql,
        columns,
        rows,
        truncated,
        changes: changes > 0 ? changes : colCount > 0 ? null : 0,
      });
    }
    return { results, error: null };
  } finally {
    if (pStmt) capi.sqlite3_finalize(pStmt);
    wasm.scopedAllocPop(stack);
  }
}

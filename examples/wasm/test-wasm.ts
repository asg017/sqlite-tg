import { assert } from "https://deno.land/std@0.211.0/assert/assert.ts";

// sqlite3.mjs is compiled with -sENVIRONMENT=web,worker. Deno 2 removed the
// `window` global and added a Node-compat `process` global; adjust both so
// emscripten's environment detection sees a web-like host.
(globalThis as { window?: unknown }).window = globalThis;
delete (globalThis as { process?: unknown }).process;
const { default: init } = await import("../../dist/.wasm/sqlite3.mjs");

// deno-lint-ignore no-explicit-any
const sqlite3 = (await init()) as any;
const db = new sqlite3.oo1.DB(":memory:");

Deno.test("sqlite-tg wasm", () => {
  const version = db.exec("select tg_version()", {
    returnValue: "resultRows",
  })[0][0];
  assert(typeof version === "string");
  assert(version.length > 0);

  const wkt = db.exec("select tg_to_wkt(tg_point(1, 2))", {
    returnValue: "resultRows",
  })[0][0];
  assert(wkt === "POINT(1 2)");
});

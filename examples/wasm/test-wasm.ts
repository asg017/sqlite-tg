import { assert } from "https://deno.land/std@0.211.0/assert/assert.ts";

// @deno-types="https://raw.githubusercontent.com/sqlite/sqlite-wasm/main/index.d.ts"
import { default as init } from "../../dist/sqlite3.mjs";

const sqlite3 = await init();
const db = new sqlite3.oo1.DB(":memory:");

Deno.test("sqlite-tg wasm", () => {
  const version = db.exec("select tg_version()", {
    returnValue: "resultRows",
  })[0][0];
  assert(typeof version === "string");
  assert(version.length > 0);
});

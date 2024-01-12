import { attach } from "@alex.garcia/sqlite-wasm-toolkit/slim";
import { default as init } from "../dist/sqlite3.mjs";
const sqlite3 = await init();
attach({
  sqlite3,
  target: document.body.querySelector("#target"),
  initialCode: `select
  rowid,
  tg_to_wkt(geometry),
  tg_to_wkb(geometry),
  tg_to_geojson(geometry)
from tg_geometries_each('
  GEOMETRYCOLLECTION (
    POINT (40 10),
    LINESTRING (10 10, 20 20, 10 40),
    POLYGON ((40 40, 20 45, 45 30, 40 40))
  )
');`,
});

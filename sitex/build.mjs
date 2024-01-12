import * as esbuild from "esbuild";
import { copyFileSync, readFileSync, writeFileSync } from "node:fs";
import { marked } from "marked";

const indexHtmlTemplate = readFileSync("index.html", "utf8");
const docsHtml = marked.parse(readFileSync("../docs.md", "utf8"));

esbuild.buildSync({
  entryPoints: ["index.js"],
  outdir: "dist",
  bundle: true,
  minify: true,
  format: "esm",
  loader: {
    ".woff": "dataurl",
    ".otf": "dataurl",
  },
});
copyFileSync("../dist/sqlite3.wasm", "dist/sqlite3.wasm");
copyFileSync(
  "node_modules/@alex.garcia/sqlite-wasm-toolkit/dist/slim.css",
  "dist/slim.css"
);
copyFileSync("./index.html", "dist/index.html");
writeFileSync(
  "./dist/index.html",
  indexHtmlTemplate.replace("$DOCS", docsHtml),
  "utf8"
);

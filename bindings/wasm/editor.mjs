import { EditorView, basicSetup } from "codemirror";
import { sql, SQLite } from "@codemirror/lang-sql";
import { keymap } from "@codemirror/view";
import { html } from "htl";
import prettyMilliseconds from "pretty-ms";

const INITIAL = `select
  rowid,
  tg_to_wkt(geometry),
  tg_to_wkb(geometry),
  tg_to_geojson(geometry)
from tg_geometries_each('GEOMETRYCOLLECTION (POINT (40 10),
LINESTRING (10 10, 20 20, 10 40),
POLYGON ((40 40, 20 45, 45 30, 40 40)))');
`;
function main(SQL) {
  var db = new SQL.Database();

  const editor = new EditorView({
    parent: document.getElementById("editor"),
    doc: INITIAL,
    extensions: [
      keymap.of([
        {
          key: "Shift-Enter",
          run: function () {
            onSubmit(editor.state.doc.toString());
            return true;
          },
        },
      ]),
      basicSetup,
      EditorView.lineWrapping,
      sql({
        dialect: SQLite,
      }),
    ],
  });

  const results = document.getElementById("results");
  function onSubmit(code) {
    console.log(code);

    while (results.firstChild) results.removeChild(results.firstChild);
    results.appendChild(html`loading...`);

    const start = performance.now();

    const rows = [];
    let columns;
    try {
      let stmt = db.prepare(code);
      columns = stmt.getColumnNames();
      while (stmt.step()) {
        rows.push(stmt.get());
      }
    } catch (error) {
      const duration = performance.now() - start;
      while (results.firstChild) results.removeChild(results.firstChild);
      results.appendChild(html`<div>
        <pre>${error.toString()}</pre>
        <div>
          Error after
          ${prettyMilliseconds(duration, { millisecondsDecimalDigits: 2 })}
        </div>
      </div>`);
      return;
    }

    const duration = performance.now() - start;

    const table = html`<table></table>`;
    table.appendChild(html`<thead>
      <tr>
        ${columns.map((column) => html`<th>${column}</th>`)}
      </tr>
    </thead>`);

    table.appendChild(html`<tbody>
      ${rows.map(
        (values) => html`<tr>
          ${values.map((_, idx) => {
            const value = values[idx];
            if (value instanceof Uint8Array) {
              return html`<td>Blob${`<${value.length}>`}</td>`;
            }
            return html`<td>${value === null ? "NULL" : values[idx]}</td>`;
          })}
        </tr>`
      )}
    </tbody>`);

    while (results.firstChild) results.removeChild(results.firstChild);
    results.appendChild(html`<div>
      ${table}
      <div style="font-style: italic; margin-top: 4px;">
        ${rows.length} row${rows.length > 1 ? "s" : ""}, completed in
        ${prettyMilliseconds(duration, { millisecondsDecimalDigits: 2 })}
      </div>
    </div>`);
  }

  onSubmit(INITIAL);
}
window
  .initSqlJs({
    locateFile: (filename, prefix) => {
      return `./${filename}`;
    },
  })
  .then(main);

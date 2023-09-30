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

  const SUBMIT_ELEMENT = document.getElementById("submit");
  const EDITOR_ELEMENT = document.getElementById("editor");
  const RESULTS_ELEMENT = document.getElementById("results");

  const editor = new EditorView({
    parent: EDITOR_ELEMENT,
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

  SUBMIT_ELEMENT.addEventListener("click", () => {
    onSubmit(editor.state.doc.toString());
  });

  function updateResults(node) {
    while (RESULTS_ELEMENT.firstChild)
      RESULTS_ELEMENT.removeChild(RESULTS_ELEMENT.firstChild);
    RESULTS_ELEMENT.appendChild(node);
  }

  function onSubmit(code) {
    console.log(code);

    updateResults(html`loading...`);

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
      updateResults(html`<div>
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

    updateResults(html`<div>
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

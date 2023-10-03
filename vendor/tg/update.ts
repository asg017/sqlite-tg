#!/usr/bin/env -S deno run --allow-write=tg.c,tg.h --allow-net=/api.github.com,raw.githubusercontent.com

/**
 *  An script to retrieve the specified version of the tg amalgamation.
 */

if (Deno.args[0] == "list" || Deno.args[0] === "l") {
  const tags = await fetch(`https://api.github.com/repos/tidwall/tg/tags`).then(
    (r) => r.json()
  );
  for (const tag of tags) {
    console.log(tag.name);
  }
  Deno.exit(0);
}

const VERSION = Deno.args[0];

if (!VERSION) {
  console.error("ERROR: Missing version number");
  console.error("Usage: ./update.ts VERSION_NUMBER");
  Deno.exit(1);
}
const data = await fetch(
  `https://api.github.com/repos/tidwall/tg/commits/${VERSION}`
).then((r) => r.json());

const sha = data.sha;
const date = data.commit.author.date;

const tgCUrl = `https://raw.githubusercontent.com/tidwall/tg/${sha}/tg.c`;
const tgHUrl = `https://raw.githubusercontent.com/tidwall/tg/${sha}/tg.h`;

const tgC = await fetch(tgCUrl).then((r) => r.text());
const tgH = await fetch(tgHUrl).then((r) => r.text());

Deno.writeTextFile("tg.c", tgC);
Deno.writeTextFile(
  "tg.h",
  `/**
* Retrieved from ${tgHUrl}
* at ${new Date().toISOString()}.
*
* The following variables are defined for sqlite-tg debugging.
*/

#define TG_VERSION "${VERSION}"
#define TG_COMMIT "${sha}"
#define TG_DATE "${date}"

// Everything after this comment is from the original tg source.
${tgH}`
);

#!/usr/bin/env -S deno run --allow-write=tg.c,tg.h --allow-read=patches --allow-run=git --allow-net=api.github.com,raw.githubusercontent.com

/**
 *  An script to retrieve the specified version of the tg amalgamation,
 *  then re-apply the local fixes in patches/.
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
const commit = await fetch(
  `https://api.github.com/repos/tidwall/tg/commits/${VERSION}`
).then((r) => r.json());

const SHA = commit.sha;
const DATE = commit.author.date;

const tgCUrl = `https://raw.githubusercontent.com/tidwall/tg/${SHA}/tg.c`;
const tgHUrl = `https://raw.githubusercontent.com/tidwall/tg/${SHA}/tg.h`;

const tgC = await fetch(tgCUrl).then((r) => r.text());
const tgH = await fetch(tgHUrl).then((r) => r.text());

await Deno.writeTextFile("tg.c", tgC);
await Deno.writeTextFile(
  "tg.h",
  `/**
* Retrieved from ${tgHUrl}
* at ${new Date().toISOString()}.
*
* The following variables are defined for sqlite-tg debugging.
*/

#define TG_VERSION "${VERSION}"
#define TG_COMMIT "${SHA}"
#define TG_DATE "${DATE}"

// Everything after this comment is from the original tg source.
${tgH}`
);

// Re-apply local fixes on top of the fresh amalgamation. Each patch in
// patches/ is a self-contained candidate upstream fix (paths are relative to
// the tg repo root, so they apply there too); drop a patch once a tg release
// includes the fix. A failed apply usually means upstream changed the
// surrounding code — re-diff the patch by hand.
//
// git apply resolves patch paths against the sqlite-tg repo root (not the
// cwd) and silently skips paths outside the current directory, hence
// --directory; without it the patches no-op with exit code 0.
const patches = [...Deno.readDirSync("patches")]
  .filter((entry) => entry.isFile && entry.name.endsWith(".patch"))
  .map((entry) => entry.name)
  .sort();
for (const patch of patches) {
  const { success, stderr } = await new Deno.Command("git", {
    args: ["apply", "--directory=vendor/tg", `patches/${patch}`],
    stderr: "piped",
  }).output();
  if (!success) {
    console.error(`ERROR: failed to apply patches/${patch}`);
    console.error(new TextDecoder().decode(stderr));
    Deno.exit(1);
  }
  console.log(`applied patches/${patch}`);
}

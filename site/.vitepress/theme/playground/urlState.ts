// Playground URL contract (see plans/site2/plan.md, "Decisions"):
// `<base>playground.html?sql=<b64>` where <b64> is base64url (RFC 4648 §5,
// no padding) of the UTF-8 SQL text, plus optional `&db=<dataset-id>`.
// Shared with ticket 06's build-time "Open in playground" fence links.

/** Encode SQL text as base64url (no padding) of its UTF-8 bytes. */
export function encodeSql(sql: string): string {
  const bytes = new TextEncoder().encode(sql);
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

/**
 * Decode a base64url `?sql=` parameter back to SQL text.
 * Returns null on malformed input instead of throwing.
 */
export function decodeSql(param: string): string | null {
  try {
    const b64 = param.replaceAll("-", "+").replaceAll("_", "/");
    const padded = b64 + "=".repeat((4 - (b64.length % 4)) % 4);
    const bin = atob(padded);
    const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
    return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    return null;
  }
}

/**
 * Build a shareable playground URL for the given SQL (and optional dataset).
 *
 * @param pageUrl absolute URL of the playground page, e.g.
 *   `location.origin + withBase("/playground.html")`
 */
export function playgroundUrl(
  pageUrl: string,
  sql: string,
  datasetId?: string | null
): string {
  let url = `${pageUrl}?sql=${encodeSql(sql)}`;
  if (datasetId) url += `&db=${encodeURIComponent(datasetId)}`;
  return url;
}

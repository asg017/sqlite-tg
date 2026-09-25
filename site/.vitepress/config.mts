import { DefaultTheme, defineConfig, HeadConfig } from "vitepress";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { tabsMarkdownPlugin } from "vitepress-plugin-tabs";

const PROJECT = "sqlite-tg";
const description = "A SQLite extension for fast geospatial operations";
// Site base path — also baked into the "Open in playground" fence links at
// build time (the markdown renderer has no access to `withBase`).
const BASE = "/sqlite-tg/";

const VERSION = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "..", "VERSION"),
  "utf8"
).trim();

const sqliteLanguage = JSON.parse(
  readFileSync(
    join(
      dirname(fileURLToPath(import.meta.url)),
      "..",
      "sqlite.tmlanguage.json"
    ),
    "utf8"
  )
);

function head(): HeadConfig[] {
  return [
    [
      "link",
      {
        rel: "shortcut icon",
        type: "image/svg+xml",
        href: "./favicon.svg",
      },
    ],
    [
      "script",
      {
        defer: "",
        "data-domain": `alexgarcia.xyz/${PROJECT}`,
        src: "https://plausible.io/js/script.js",
      },
    ],
  ];
}

function nav(): DefaultTheme.NavItem[] {
  return [
    { text: "API Reference", link: "/api-reference" },
    { text: "Playground", link: "/playground" },
    { text: "♥ Sponsor", link: "https://github.com/sponsors/asg017" },
    {
      text: `v${VERSION}`,
      items: [
        {
          text: "Github Release",
          link: `https://github.com/asg017/${PROJECT}/releases/v${VERSION}`,
        },
        {
          text: "Bindings",
          items: [
            {
              text: "Python: PyPi package",
              link: `https://pypi.org/project/${PROJECT}`,
            },
            {
              text: "Node.js: NPM package",
              link: `https://www.npmjs.com/package/${PROJECT}`,
            },
            {
              text: "Ruby: Ruby gem",
              link: `https://rubygems.org/gems/${PROJECT}`,
            },
            {
              text: "Datasette: Plugin",
              link: `https://datasette.io/plugins/datasette-${PROJECT}`,
            },
            {
              text: "sqlite-utils: Plugin",
              link: `https://datasette.io/plugins/sqlite-utils-${PROJECT}`,
            },
          ],
        },
      ],
    },
  ];
}

function sidebar(): DefaultTheme.SidebarItem[] {
  return [
    {
      text: "Getting Started",
      items: [
        {
          text: "Introduction",
          link: "/introduction",
        },
        {
          text: "Installation",
          link: "/installation",
        },
      ],
    },
    {
      text: "Documentation",
      items: [
        { text: "API Reference", link: "/api-reference" },
        { text: "Playground", link: "/playground" },
      ],
    },
    {
      text: "See also",
      items: [
        {
          text: "sqlite-ecosystem",
          link: "https://github.com/asg017/sqlite-ecosystem",
        },
        {
          text: "sqlite-vec",
          link: "https://github.com/asg017/sqlite-vec",
        },
      ],
    },
  ];
}

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: `${PROJECT}`,
  description,
  lastUpdated: true,
  head: head(),
  base: BASE,
  rewrites: {
    "getting-started/:pkg.md": ":pkg.md",
  },
  themeConfig: {
    nav: nav(),
    sidebar: sidebar(),
    footer: {
      message: "MIT/Apache-2 License",
      copyright:
        'Copyright © 2024 <a href="https://alexgarcia.xyz/">Alex Garcia</a>',
    },
    outline: "deep",
    search: {
      provider: "local",
    },
    socialLinks: [
      { icon: "github", link: `https://github.com/asg017/${PROJECT}` },
    ],
    editLink: {
      pattern: `https://github.com/asg017/${PROJECT}/edit/main/site/:path`,
    },
  },
  markdown: {
    languages: [sqliteLanguage],
    config(md) {
      md.use(tabsMarkdownPlugin);

      // "Open in playground" links on ```sql / ```sqlite fences
      // (plans/site2/tickets/06-open-in-playground.md). URL contract shared
      // with theme/playground/urlState.ts: `?sql=<base64url no-pad UTF-8>`
      // plus optional `&db=<dataset id>`. Fence info string extras:
      //   ```sql db=us-states   → pre-loads that sample dataset
      //   ```sql no-playground  → no link
      const defaultFence = md.renderer.rules.fence!;
      md.renderer.rules.fence = (tokens, idx, options, env, self) => {
        const rendered = defaultFence(tokens, idx, options, env, self);
        const [lang, ...attrs] = tokens[idx].info.trim().split(/\s+/);
        if (
          (lang !== "sql" && lang !== "sqlite") ||
          attrs.includes("no-playground")
        ) {
          return rendered;
        }
        const db = attrs.find((attr) => attr.startsWith("db="))?.slice(3);
        // `playground.html` (not `/playground`) so the link also resolves on
        // static hosts like GitHub Pages without a trailing-slash redirect.
        let href = `${BASE}playground.html?sql=${Buffer.from(
          tokens[idx].content,
          "utf8"
        ).toString("base64url")}`;
        if (db) href += `&db=${encodeURIComponent(db)}`;
        return (
          `<div class="playground-fence">${rendered}` +
          `<div class="open-in-playground">` +
          `<a href="${md.utils.escapeHtml(href)}">Open in playground ↗</a>` +
          `</div></div>`
        );
      };
    },
  },
});

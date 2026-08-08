# Installation

Install `sqlite-tg` with your language or tool of choice:

<!--@include: ../installing.md.tmpl -->

## Loadable extension

Every [GitHub release](https://github.com/asg017/sqlite-tg/releases) also
includes pre-compiled loadable extensions for macOS, Linux, and Windows
(a `tg0.dylib`/`tg0.so`/`tg0.dll` file), which can be loaded into any SQLite
client that supports run-time loadable extensions, such as the `sqlite3` CLI:

```
.load ./tg0

select tg_version();
```

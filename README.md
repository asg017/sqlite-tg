# sqlite-tg

Work-in-progress geospatial SQLite extension around [tg](https://github.com/tidwall/tg). Not ready yet, but feel free to play with it!

Once stabilized, will be a part of [sqlite-ecosystem](https://github.com/asg017/sqlite-ecosystem).

## Usage

```sql
.load ./tg0

select tg_point_wkt(1, 2);
-- 'POINT(1 2)'
```

tg and therefore `sqlite-tg` support WKT, WKB, and GeoJSON. Most functions will accept any of these formats, and you can convert between them with [`tg_to_geojson()`](./docs.md#tg_to_geojson), [`tg_to_wkb()`](./docs.md#tg_to_wkb), and [`tg_to_wkt()`](./docs.md#tg_to_wkt).

```sql
select tg_to_geojson('POINT(1 2)');
-- '{"type":"Point","coordinates":[1,2]}'

select tg_to_wkb('POINT(1 2)');
-- X'0101000000000000000000f03f0000000000000040'

select tg_to_wkt('{"type":"Point","coordinates":[1,2]}');
-- 'POINT(1 2)'
```

## Documentation

See [`docs.md`](./docs.md) for a full API reference.

## Installing

| Language       | Install                                                  |                                                                                                                                                                                                     |
| -------------- | -------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Python         | `pip install sqlite-tg`                                  | [![PyPI](https://img.shields.io/pypi/v/sqlite-tg.svg?color=blue&logo=python&logoColor=white)](https://pypi.org/project/sqlite-tg/)                                                                  |
| Datasette      | `datasette install datasette-sqlite-tg`                  | [![Datasette](https://img.shields.io/pypi/v/datasette-sqlite-tg.svg?color=B6B6D9&label=Datasette+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/datasette-sqlite-tg)             |
| sqlite-utils   | `sqlite-utils install sqlite-utils-sqlite-tg`            | [![sqlite-utils](https://img.shields.io/pypi/v/sqlite-utils-sqlite-tg.svg?color=B6B6D9&label=sqlite-utils+plugin&logoColor=white&logo=python)](https://datasette.io/plugins/sqlite-utils-sqlite-tg) |
| Node.js        | `npm install sqlite-tg`                                  | [![npm](https://img.shields.io/npm/v/sqlite-tg.svg?color=green&logo=nodedotjs&logoColor=white)](https://www.npmjs.com/package/sqlite-tg)                                                            |
| Deno           | [`deno.land/x/sqlite_tg`](https://deno.land/x/sqlite_tg) | [![deno.land/x release](https://img.shields.io/github/v/release/asg017/sqlite-tg?color=fef8d2&include_prereleases&label=deno.land%2Fx&logo=deno)](https://deno.land/x/sqlite_tg)                    |
| Ruby           | `gem install sqlite-tg`                                  | [![Gem](https://img.shields.io/gem/v/sqlite-tg?color=red&logo=rubygems&logoColor=white)](https://rubygems.org/gems/sqlite-tg)                                                                                                              |
| Github Release |                                                          | ![GitHub tag (latest SemVer pre-release)](https://img.shields.io/github/v/tag/asg017/sqlite-tg?color=lightgrey&include_prereleases&label=Github+release&logo=github)                                |

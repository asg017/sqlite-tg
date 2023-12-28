#!/bin/bash

bench_build() {
  hyperfine --prepare 'rm tmp-*.db' \
    'sqlite3 tmp-rtree.db ".read build-rtree.sql"' \
    'sqlite3 tmp-tg.db ".read build-tg.sql"'
}
bench_query() {
  hyperfine --warmup 10 --min-runs=200 \
    'sqlite3 tmp-rtree.db ".read query-rtree.sql"' \
    'sqlite3 tmp-tg.db ".read query-tg.sql"' \
    'sqlite3 tmp-tg.db ".read query-tg2.sql"'
}

#bench_build
bench_query

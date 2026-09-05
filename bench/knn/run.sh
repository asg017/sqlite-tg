#!/bin/bash
# Runs bench.py with its Python dependencies resolved by uv.
cd "$(dirname "$0")"
exec uv run --with duckdb --with scipy --with scikit-learn --with numpy --with "psycopg[binary]" python bench.py "$@"

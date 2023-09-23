#!/bin/bash

set -euo pipefail

export PACKAGE_NAME="sqlite-tg"
export VERSION=$(cat VERSION)

envsubst < bindings/deno/deno.json.tmpl > bindings/deno/deno.json
echo "✅ generated deno/deno.json"

envsubst < bindings/deno/README.md.tmpl > bindings/deno/README.md
echo "✅ generated deno/README.md"

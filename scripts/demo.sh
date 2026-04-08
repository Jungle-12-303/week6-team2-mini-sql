#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DB="$ROOT_DIR/.artifacts/runtime/demo-db"

rm -rf "$TMP_DB"
mkdir -p "$TMP_DB"
cp -R "$ROOT_DIR/db/." "$TMP_DB"

echo "Running demo with temporary DB: $TMP_DB"
"$ROOT_DIR/mini_sql" --db "$TMP_DB" "$ROOT_DIR/examples/demo.sql"

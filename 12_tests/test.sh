#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DATA_FILE="$ROOT_DIR/03_data/materials.csv"
BACKUP_FILE="$ROOT_DIR/03_data/materials.csv.bak"

cleanup() {
    if [ -f "$BACKUP_FILE" ]; then
        mv "$BACKUP_FILE" "$DATA_FILE"
    fi
}

trap cleanup EXIT

cp "$DATA_FILE" "$BACKUP_FILE"

cat <<'EOF' > "$DATA_FILE"
LX2 sunvisor,ASD,MMH,21G
CN7 garnish,PPC,BLK,35G
EOF

echo "[1] build"
make -C "$ROOT_DIR" rebuild

echo "[2] insert"
"$ROOT_DIR/mini_sql_rebuild" "$ROOT_DIR/01_insert_sql/insert_material.sql"

echo "[3] select"
"$ROOT_DIR/mini_sql_rebuild" "$ROOT_DIR/02_select_sql/select_materials.sql"

echo "[4] done"

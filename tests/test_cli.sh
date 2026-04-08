#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/db"

cat > "$TMP_DIR/db/users.schema" <<'EOF'
id,name,age,team
EOF

cat > "$TMP_DIR/demo.sql" <<'EOF'
INSERT INTO users (id, name, age, team) VALUES (1, 'Alice', 24, 'backend');
INSERT INTO users (id, name, age, team) VALUES (2, 'Bob', 26, 'infra');
SELECT name, team FROM users WHERE age = 26;
EOF

OUTPUT="$("$ROOT_DIR/mini_sql" --db "$TMP_DIR/db" "$TMP_DIR/demo.sql")"

printf '%s\n' "$OUTPUT"
printf '%s' "$OUTPUT" | grep -q "INSERT 1"
printf '%s' "$OUTPUT" | grep -q "Bob"
printf '%s' "$OUTPUT" | grep -q "infra"
printf '%s' "$OUTPUT" | grep -q "(1 row)"

REPL_OUTPUT="$(printf "INSERT INTO users (id, name, age, team) VALUES (3, 'Carol', 27, 'platform');\nSELECT name, team FROM users WHERE age = 27;\n.exit\n" | "$ROOT_DIR/mini_sql" --db "$TMP_DIR/db")"

printf '%s\n' "$REPL_OUTPUT"
printf '%s' "$REPL_OUTPUT" | grep -q "Carol"
printf '%s' "$REPL_OUTPUT" | grep -q "platform"
printf '%s' "$REPL_OUTPUT" | grep -q "(1 row)"
grep -q "3,Carol,27,platform" "$TMP_DIR/db/users.data"

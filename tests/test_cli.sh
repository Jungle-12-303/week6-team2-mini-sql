#!/usr/bin/env bash
set -euo pipefail

# 실제 사용 흐름과 가장 비슷한 방식으로 CLI 를 검증한다.
# 1. 여러 SQL 파일 실행
# 2. 인자 순서와 --db 검증
# 3. CREATE/INSERT/DELETE/SELECT/DROP 결과 확인

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/db"
cat > "$TMP_DIR/step1.sql" <<'EOF'
CREATE TABLE users (id INT, name TEXT, age INT, team TEXT);
INSERT INTO users (id, name, age, team) VALUES (1, 'Alice', 24, 'backend');
EOF

cat > "$TMP_DIR/step2.sql" <<'EOF'
INSERT INTO users (id, name, age, team) VALUES (2, 'Bob', 26, 'infra');
DELETE FROM users WHERE age = 24;
SELECT name, team FROM users WHERE age = 26;
EOF

OUTPUT="$("$ROOT_DIR/mini_sql" "$TMP_DIR/step1.sql" --db "$TMP_DIR/db" "$TMP_DIR/step2.sql")"

printf '%s\n' "$OUTPUT"
printf '%s' "$OUTPUT" | grep -q "CREATE TABLE"
printf '%s' "$OUTPUT" | grep -q "INSERT 1"
printf '%s' "$OUTPUT" | grep -q "DELETE 1"
printf '%s' "$OUTPUT" | grep -q "Bob"
printf '%s' "$OUTPUT" | grep -q "infra"
printf '%s' "$OUTPUT" | grep -q "(1개 행)"

if "$ROOT_DIR/mini_sql" --db "$TMP_DIR/missing-db" "$TMP_DIR/step1.sql" >"$TMP_DIR/error.log" 2>&1; then
  echo "없는 DB 경로 검증이 실패해야 합니다"
  exit 1
fi
grep -q "데이터베이스 디렉터리가 존재하지 않습니다" "$TMP_DIR/error.log"

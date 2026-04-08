# Mini SQL Processor in C

텍스트 파일로 작성한 SQL을 CLI로 전달받아 파싱하고, 파일 기반 DB에 반영하는 작은 SQL 처리기입니다.

과제 요구사항에 맞춰 `INSERT`, `SELECT`만 최소 구현했지만, 문법 표면은 실제 SQL Server/T-SQL 스타일에 가깝게 유지했습니다.

## Why This Shape

- 핵심 학습 흐름을 그대로 드러냅니다.
  `입력(SQL 파일) -> 토크나이징 -> 파싱(AST) -> 실행 -> 파일 저장/조회`
- 발표에서 설명하기 쉽습니다.
  SQL 문법 처리와 DB 실행 흐름이 파일 단위로 분리되어 있습니다.
- 실제 SQL과 닮은 사용감을 유지합니다.
  `INSERT INTO ... VALUES (...)`, `SELECT ... FROM ... WHERE ... = ...`, 세미콜론, 문자열 리터럴, `--` 주석을 지원합니다.

## Supported SQL

### INSERT

```sql
INSERT INTO users VALUES (1, 'Alice', 24, 'backend');
INSERT INTO users (id, name, age, track) VALUES (2, 'Bob', 26, 'database');
```

### SELECT

```sql
SELECT * FROM users;
SELECT name, track FROM users WHERE age = 26;
```

### Additional Support

- 여러 SQL 문장을 한 파일에서 순차 실행
- `--` 한 줄 주석
- 문자열 리터럴의 작은따옴표 이스케이프
  예: `'O''Reilly'`
- schema-qualified table name
  예: `analytics.events`

## Project Structure

```text
.
├── db/                 # 기본 데모용 schema/data
├── examples/           # 예제 SQL 파일
├── include/
│   └── mini_sql.h      # 공용 타입/함수 선언
├── scripts/
│   └── demo.sh         # 발표용 데모 실행 스크립트
├── src/
│   ├── main.c          # CLI 엔트리포인트
│   ├── lexer.c         # SQL -> Token
│   ├── parser.c        # Token -> AST
│   ├── executor.c      # AST 실행 오케스트레이션
│   ├── storage.c       # schema/data 파일 처리, CSV I/O, SELECT 출력
│   └── utils.c         # 공통 유틸리티
├── tests/
│   ├── test_suite.c    # 단위 + 통합 테스트
│   └── test_cli.sh     # CLI 기능 테스트
└── Makefile
```

## Storage Design

테이블은 파일로 관리합니다.

- 스키마 파일: `db/<table>.schema`
- 데이터 파일: `db/<table>.data`

### Example

`db/users.schema`

```text
id,name,age,track
```

`db/users.data`

```text
1000,seed,25,core
1001,Alice,24,backend
```

### Rules

- `.schema` 첫 줄은 컬럼 이름 목록입니다.
- `.data` 는 CSV 포맷입니다.
- 문자열에 쉼표나 큰따옴표가 있으면 CSV 규칙으로 escape 됩니다.
- `analytics.events` 같은 이름은 `db/analytics/events.schema` 로 매핑됩니다.

## Build

```bash
make
```

## Docker / Linux Environment

현재 로컬 맥북에서 바로 빌드하면 `Apple clang` 과 `arm64-apple-darwin` 타깃을 사용합니다.
즉, macOS ARM 환경에서 컴파일되는 것이고 Linux가 아닙니다.

Docker로 들어오면 OS는 Linux로 바뀝니다.
Apple Silicon 맥에서는 이때도 아키텍처가 `aarch64` 로 보이는 것이 정상입니다.

Linux 컨테이너 환경으로 맞추려면 아래처럼 실행합니다.

```bash
make docker-build
make docker-test
make cli
```

설정의 기준은 `.devcontainer/` 입니다.

- `.devcontainer/docker-compose.yml`
- `.devcontainer/devcontainer.json`
- `.devcontainer/bootstrap.sh`

- OS: Ubuntu Linux container
- Runtime: Docker Linux container
- Toolchain: `build-essential`

현재 `.devcontainer` 로 올라온 컨테이너는 아래 명령으로 확인할 수 있습니다.

```bash
docker compose -f .devcontainer/docker-compose.yml ps
```

### VS Code

VS Code에서 빨간 줄을 없애고 Linux 헤더 기준으로 작업하려면:

1. Docker Desktop 실행
2. VS Code에서 이 폴더 열기
3. `Dev Containers: Reopen in Container` 실행

그러면 컨테이너 안에서 `/usr/bin/cc` 와 Linux 시스템 헤더를 기준으로 IntelliSense가 동작합니다.

### Why The Include Errors Happened

스크린샷의 오류는 코드 문법 오류라기보다 VS Code IntelliSense 설정 문제입니다.

- 현재 호스트는 `Darwin arm64`
- 컴파일러는 `Apple clang`
- VS Code가 프로젝트용 include path 와 시스템 헤더 기준을 아직 못 잡은 상태

그래서 `mini_sql.h`, `stdio.h`, `unistd.h` 같은 헤더를 “못 찾는다”고 표시한 것입니다.
실제 빌드는 될 수 있어도, 에디터는 별도 설정이 없으면 이런 빨간 줄을 띄울 수 있습니다.

## Run

기본 DB 경로는 `./db` 입니다.

### Interactive CLI

아무 SQL 파일도 넘기지 않으면 인터랙티브 모드로 실행됩니다.

```bash
make cli
```

이제 `make cli` 는 Docker Linux 환경에서 인터랙티브 CLI를 실행합니다.

로컬 맥 환경에서 직접 실행하려면 아래 명령을 사용합니다.

```bash
./mini_sql --db ./db
```

예시:

```text
mini_sql> INSERT INTO users (id, name, age, track) VALUES (1003, 'Carol', 27, 'infra');
INSERT 1
mini_sql> SELECT name, track FROM users WHERE age = 27;
+-------+-------+
| name  | track |
+-------+-------+
| Carol | infra |
+-------+-------+
(1 row)
mini_sql> .exit
```

각 SQL 문장은 `;` 로 끝내면 바로 실행됩니다.

- `Left/Right` 방향키로 커서 이동
- `Up/Down` 방향키로 이전 명령 히스토리 탐색
- 종료는 `.exit`, `exit`, `quit`, 또는 `Ctrl-D`

### SQL File

```bash
./mini_sql ./examples/demo.sql
```

다른 DB 디렉터리를 사용하려면 `--db` 옵션을 사용합니다.

```bash
./mini_sql --db ./db ./examples/demo.sql
```

## Demo

```bash
make demo
```

`make demo` 는 원본 `db/` 를 직접 수정하지 않도록 임시 DB를 복사해서 실행합니다.

## Example Output

```text
INSERT 1
INSERT 1
+------+-------+-----+----------+
| id   | name  | age | track    |
+------+-------+-----+----------+
| 1000 | seed  | 25  | core     |
| 1001 | Alice | 24  | backend  |
| 1002 | Bob   | 26  | database |
+------+-------+-----+----------+
(3 rows)
+------+----------+
| name | track    |
+------+----------+
| Bob  | database |
+------+----------+
(1 row)
```

## Test

```bash
make test
```

### Test Coverage

- 단위 테스트
  - 토크나이저가 주석과 문자열을 올바르게 인식하는지 검증
  - 파서가 여러 문장과 `WHERE` 절을 올바르게 AST로 만드는지 검증
- 통합 테스트
  - `INSERT -> 파일 저장 -> SELECT -> 출력` 전체 흐름 검증
  - schema-qualified table name 검증
  - CSV escape 검증
- 기능 테스트
  - 실제 CLI 실행으로 SQL 파일 처리 결과 검증

## Edge Cases Considered

- 마지막 문장 뒤 세미콜론 유무
- 여러 개의 SQL 문장을 한 파일에 작성하는 경우
- 알 수 없는 컬럼 이름
- 스키마와 INSERT 값 개수 불일치
- CSV 내부의 쉼표/큰따옴표
- 빈 데이터 파일 또는 아직 `.data` 파일이 없는 테이블
- `WHERE column = value` 단일 조건 필터

## What We Deliberately Did Not Implement

과제의 핵심을 흐리지 않기 위해 아래는 제외했습니다.

- `CREATE TABLE`
- `UPDATE`, `DELETE`
- `JOIN`, `ORDER BY`, `GROUP BY`
- 타입 시스템과 형변환
- 복합 `WHERE`
- 인덱스/트랜잭션/동시성 처리

## Core Learning Points

이 프로젝트에서 설명할 수 있어야 하는 핵심은 아래 네 가지입니다.

1. SQL을 문자열로 받았을 때 바로 실행하지 않고, 먼저 `Token` 으로 쪼개고 `AST` 로 구조화한다.
2. `INSERT` 는 AST를 스키마와 대조한 뒤 CSV row로 직렬화해서 파일에 append 한다.
3. `SELECT` 는 파일을 다시 읽어 row 단위로 복원하고, 필요한 컬럼과 `WHERE` 조건만 적용해 출력한다.
4. 실제 DBMS는 훨씬 복잡하지만, 이 미니 프로젝트만으로도 파서와 실행기 분리 원리를 충분히 체감할 수 있다.

## 발표 포인트 추천

4분 발표라면 아래 순서가 가장 깔끔합니다.

1. 문제 정의
   SQL 파일을 입력받아 파일 기반 DB에서 실행하는 처리기 구현
2. 아키텍처
   `lexer -> parser -> executor -> storage`
3. 데모
   `INSERT` 두 번, `SELECT *`, `SELECT ... WHERE ...`
4. 검증
   `make test` 결과와 엣지 케이스 설명
5. 한계와 확장
   다음 단계로 `UPDATE`, `DELETE`, 복합 조건, 타입 시스템 확장 가능

## Inspiration

문법 표면은 Microsoft의 SQL Server 샘플 저장소에 나오는 일반적인 T-SQL 사용 방식과 비슷하게 맞췄습니다.

- [microsoft/sql-server-samples](https://github.com/microsoft/sql-server-samples)

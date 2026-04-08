# 학습용 파일 기반 SQL Processor MVP

## 1단계. 설계 요약

- 지원 SQL
  - `INSERT INTO users VALUES (1, 'Alice', 20);`
  - `SELECT * FROM users;`
  - `SELECT name, id FROM users;`
- 저장 포맷
  - `data/<table>.csv`
  - 첫 줄(header)을 스키마처럼 사용
- 파일 구조
  - `main.c`: CLI 진입점
  - `parser.c`: SQL 문자열 파싱
  - `executor.c`: 파싱 결과 실행
  - `storage.c`: CSV 파일 읽기/쓰기
- 이 구성이 MVP에 적절한 이유
  - 입력 → 파싱 → 실행 → 저장/조회 → 출력 흐름이 파일별로 분리되어 보여서 학습하기 쉽다.
  - 무거운 파서 도구 없이 `ctype.h`, `string.h`, `fopen/fgets/fputs` 수준으로 끝난다.

## 2단계. 프로젝트 구조 제안

이번 구현은 "조금 정리된 최소 버전"으로 구성했다.

- `main.c`
- `common.h`
- `parser.h`
- `parser.c`
- `executor.h`
- `executor.c`
- `storage.h`
- `storage.c`
- `tests.c`
- `Makefile`
- `README.md`
- `data/users.csv`
- `examples/insert_user.sql`
- `examples/select_users.sql`

## 3단계. 전체 코드 설명

### `main.c`

- 프로그램 인자를 검사한다.
- SQL 파일 전체를 읽는다.
- `parse_sql()`로 문장을 해석한다.
- `execute_statement()`로 실행한다.

### `parser.c`

- `INSERT` 와 `SELECT` 두 문장만 지원한다.
- 키워드는 대소문자를 구분하지 않는다.
- `INSERT` 는 `VALUES (...)` 안의 값을 하나씩 잘라서 저장한다.
- 작은따옴표 문자열은 `'Alice'` → `Alice` 형태로 저장한다.
- `SELECT *` 뿐 아니라 컬럼 목록 선택도 지원한다.

### `executor.c`

- 파싱 결과가 `INSERT` 인지 `SELECT` 인지 보고 storage 함수를 호출한다.
- 성공 메시지나 조회 결과를 CLI에 출력한다.

### `storage.c`

- `data/<table>.csv` 경로를 만들고 파일을 연다.
- `INSERT` 시 CSV 헤더 컬럼 개수와 값 개수를 비교한다.
- `INSERT` 시 `id` 컬럼이 있으면 중복 값을 검사한다.
- `SELECT` 시 파일 전체를 읽거나, 요청한 컬럼만 골라 출력하고 마지막에 `Rows: N` 을 붙인다.

## 4단계. 실행 방법

### 컴파일

```bash
make
```

### 테스트 실행

```bash
make test
```

### 실행 예시

먼저 `data/users.csv` 의 첫 줄은 이미 준비되어 있어야 한다.

```csv
id,name,age
```

INSERT 예시:

```bash
./sql_processor examples/insert_user.sql
```

`examples/insert_user.sql`:

```sql
INSERT INTO users VALUES (1, 'Alice', 20);
```

예상 출력:

```text
Inserted 1 row into users
```

SELECT 예시:

```bash
./sql_processor examples/select_users.sql
```

`examples/select_users.sql`:

```sql
SELECT * FROM users;
```

예상 출력:

```text
id,name,age
1,Alice,20
Rows: 1
```

특정 컬럼 조회 예시:

```sql
SELECT name, id FROM users;
```

예상 출력:

```text
name,id
Alice,1
Rows: 1
```

## 5단계. 테스트 케이스

`tests.c` 에 아래 케이스가 포함되어 있다.

- 정상 케이스
  - 소문자 `insert` 파싱
  - `INSERT` 후 `SELECT` 결과 검증
- 잘못된 SQL 형식
  - `INSERT users VALUES (1);`
  - `SELECT users;`
  - 세미콜론 누락
- 존재하지 않는 파일/테이블
  - 없는 `users.csv` 에 대한 `SELECT`
- 빈 SQL
  - 공백만 있는 문자열
- INSERT 후 SELECT 검증
  - CSV에 실제로 row가 추가되고 출력에 나타나는지 확인
- 컬럼 수 불일치
  - 헤더와 INSERT 값 개수가 다른 경우

## 6단계. 발표용 설명 포인트

### 입력 → 파싱 → 실행 → 저장 흐름

1. 사용자가 SQL 파일을 넘긴다.
2. `main.c` 가 파일을 읽는다.
3. `parser.c` 가 `Statement` 구조체로 바꾼다.
4. `executor.c` 가 `storage.c` 를 호출한다.
5. `storage.c` 가 CSV 파일에 append 하거나 전체 조회한다.
6. 결과를 CLI에 출력한다.

### 왜 CSV를 선택했는지

- 텍스트 파일이라 눈으로 바로 확인할 수 있다.
- `INSERT` 는 append 만 하면 되고, `SELECT *` 는 전체 읽기만 하면 된다.
- 학습용 MVP에서 저장 흐름을 설명하기 가장 쉽다.

### 지원하는 SQL / 미지원 SQL

지원:

- `INSERT INTO table VALUES (...);`
- `SELECT * FROM table;`
- `SELECT col1, col2 FROM table;`

미지원:

- `CREATE TABLE`
- `WHERE`
- `UPDATE`, `DELETE`
- JOIN, ORDER BY, GROUP BY
- 문자열 escape, 값 내부 쉼표

### 엣지 케이스 처리

- 세미콜론이 없으면 에러
- SQL이 비어 있으면 에러
- 여러 문장이 들어오면 에러
- 테이블 파일이 없으면 에러
- CSV 헤더 컬럼 수와 INSERT 값 수가 다르면 에러
- `id` 컬럼 값이 중복되면 INSERT 에러

### 추후 확장 방향

- `SELECT * FROM users WHERE id = 1;`
- `INSERT INTO users (id, name, age) VALUES (...)`
- 간단한 타입 검증
- 별도 schema 파일
- `UPDATE`, `DELETE`

## 핵심 이해 포인트

- 이 프로젝트는 "DBMS" 가 아니라 "SQL 흐름을 보여주는 작은 처리기" 다.
- 핵심은 자료구조 하나(`Statement`)로 파싱 결과를 표현하고, 실행과 저장을 분리한 점이다.
- CSV 헤더를 스키마처럼 활용해서 CREATE TABLE 없이도 전체 흐름을 구현했다.

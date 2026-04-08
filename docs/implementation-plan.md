# Mini SQL 처리기 구현 계획서

## 1. 문서 목적

이 문서는 현재 프로젝트를 "과제 제출용 구현" 수준에서 멈추지 않고,
아래 목표를 만족하는 방향으로 안정적으로 완성하기 위한 기준 문서다.

- 구조가 한눈에 읽히는가
- 각 계층의 역할이 분리되어 있는가
- 실제 SQL 엔진처럼 확장 여지가 있는가
- 발표와 유지보수 모두에 유리한가
- 코드 한 줄 한 줄을 설명할 수 있는가

이 문서는 구현 전에 보는 계획서이면서,
구현 중에는 우선순위 판단 기준,
구현 후에는 완료 여부를 체크하는 기준 문서로 사용한다.

---

## 2. 최종 목표

최종적으로 만들고 싶은 시스템은 다음과 같다.

1. 사용자는 CLI 또는 SQL 파일로 SQL을 입력할 수 있다.
2. 입력은 먼저 `SqlRunRequest`라는 실행 요청으로 정규화된다.
3. 입력 모드에 맞는 `SqlRunner`가 생성된다.
3. 프런트엔드는 입력 문자열을 토큰화하고 AST로 변환한다.
4. 실행기는 AST를 보고 명령 종류별 실행 파일로 위임한다.
5. 카탈로그는 테이블 스키마 메타데이터를 담당한다.
6. 스토리지는 실제 저장 형식을 담당한다.
7. 결과 계층은 SELECT 출력 형식을 담당한다.

핵심 흐름은 아래 한 줄로 읽혀야 한다.

`입력된 내용 -> SqlRunRequest -> SqlRunner -> SqlSession -> SqlFrontend -> StatementList(AST) -> SqlExecutor -> 각 statement 실행 -> Catalog/Storage/Result`

---

## 3. 설계 원칙

### 3.1 최우선 원칙

- 상위 함수는 "무엇을 하는지"만 보여야 한다.
- 하위 함수는 "어떻게 하는지"를 담당해야 한다.
- 같은 로직이 두 번 이상 나오면 재사용 가능성을 먼저 검토한다.
- 재사용 가치가 없으면 함수를 과도하게 잘게 나누지 않는다.
- 죽은 코드, 쓰이지 않는 구조체, 의미 없는 추상화는 남기지 않는다.

### 3.2 가독성 원칙

- 한 파일의 대표 함수는 위에서 아래로 읽었을 때 처리 흐름이 보여야 한다.
- 대표 함수 안에는 긴 메모리 조작, 반복문, 분기문이 많이 들어가지 않게 한다.
- 에러 메시지와 주석은 모두 한글로 작성한다.
- SQL 키워드와 자료형 이름처럼 코드 의미를 직접 드러내는 부분만 영어를 유지한다.

### 3.3 확장성 원칙

- 입력 방식 추가는 `session` 계층만 건드리도록 한다.
- SQL 문법 추가는 `frontend`와 `executor/statements` 중심으로 끝나야 한다.
- 저장 포맷 변경은 `storage` 구현체 교체로 흡수되어야 한다.
- 출력 포맷 변경은 `result` 계층에서 흡수되어야 한다.

---

## 4. 범위

### 4.1 이번 단계에서 반드시 구현할 범위

- CLI 입력
- SQL 파일 입력
- `CREATE TABLE`
- `INSERT`
- `SELECT`
- `DELETE`
- `DROP TABLE`
- CSV 기반 저장
- 한글 에러 메시지
- 한글 주석
- 테스트
- 정적 분석

### 4.2 이번 단계에서 자리만 남겨둘 범위

- 소켓 입력
- 업데이트 루프 입력
- 바이너리 저장 엔진
- B+Tree 저장 엔진
- 원격 저장 엔진
- 트랜잭션 관리자
- JSON/CSV/TSV 결과 출력 포맷

즉, 이번 단계는 "실제로 동작하는 최소 엔진"과
"이후 확장을 위한 안정적인 골격"을 동시에 만드는 것이 목표다.

---

## 5. 목표 디렉터리 구조

```text
src/
├── main.c
├── app/
│   └── sql_app.c
├── session/
│   ├── sql_runner.c
│   ├── sql_runner.h
│   ├── sql_session.c
│   ├── sql_session.h
│   ├── sql_cli.c
│   ├── sql_cli.h
│   ├── history.c
│   └── history.h
├── frontend/
│   ├── sql_frontend.c
│   ├── sql_frontend.h
│   ├── sql_lexer.c
│   └── sql_parser.c
├── executor/
│   ├── sql_executor.c
│   ├── sql_executor.h
│   └── statements/
│       ├── create_table_statement.c
│       ├── insert_statement.c
│       ├── select_statement.c
│       ├── delete_statement.c
│       ├── drop_table_statement.c
│       ├── sql_statement_handlers.h
│       ├── sql_statement_support.c
│       └── sql_statement_support.h
├── catalog/
│   ├── schema_catalog.c
│   └── schema_catalog.h
├── storage/
│   ├── storage_engine.c
│   ├── storage_engine.h
│   ├── storage_path.c
│   ├── storage_path.h
│   ├── csv_codec.c
│   └── csv_codec.h
├── result/
│   ├── result_table.c
│   └── result_table.h
└── common/
    └── sql_common.c
```

---

## 6. 계층별 책임

### 6.1 `app`

역할:

- 애플리케이션 조립
- 생명주기 관리
- 세션, 프런트엔드, 실행기, 저장 엔진 연결

규칙:

- 비즈니스 로직 금지
- SQL 해석 금지
- 파일 입출력 금지

대표 흐름:

`설정 검증 -> 앱 메모리 생성 -> 실행 문맥 준비 -> 런타임 구성요소 초기화`

### 6.2 `session`

역할:

- 실행 요청(`SqlRunRequest`) 생성
- 실행기(`SqlRunner`) 생성
- 입력 한 건을 세션 요청으로 변환
- CLI와 파일 입력의 공통 실행 창구
- 향후 socket/update loop 입력 추가 지점

규칙:

- SQL 문법 해석 금지
- 저장 형식 상세 구현 금지

대표 흐름:

`argv -> SqlRunRequest -> SqlRunner -> SqlInput -> 프런트엔드 전달 -> 실행기 전달 -> 오류 문맥 정리`

### 6.3 `frontend`

역할:

- 토큰화
- 파싱
- AST 생성

규칙:

- 실행 금지
- 파일 입출력 금지
- 저장 엔진 직접 호출 금지

대표 흐름:

`SQL 문자열 -> TokenList -> StatementList(AST)`

### 6.4 `executor`

역할:

- `StatementType`에 따라 실행 파일을 선택
- 명령 종류별 실행 책임 분배

규칙:

- 상위 파일은 분배만 담당
- 실제 명령 처리 로직은 `statements/` 아래에 둔다

대표 흐름:

`StatementList -> statement handler 선택 -> 각 statement 실행`

### 6.5 `catalog`

역할:

- `.schema` 저장/복원
- 컬럼 이름/타입 메타데이터 관리

규칙:

- 실제 데이터 행 저장 금지
- SQL 토큰 해석 금지

### 6.6 `storage`

역할:

- 저장 엔진 인터페이스
- 현재는 CSV 기반 파일 구현
- 향후 binary/B+Tree/remote 확장 지점

규칙:

- SQL 파싱 금지
- 출력 포맷 처리 금지

대표 흐름:

`schema/data 경로 계산 -> 파일 읽기/쓰기 -> 행 순회/삭제/추가`

### 6.7 `result`

역할:

- SELECT 결과를 어떤 형태로 보여줄지 결정
- 현재는 ASCII 표 출력만 구현

규칙:

- SQL 해석 금지
- 파일 접근 금지

---

## 7. 요청 처리 흐름

### 7.1 CLI 입력

1. `main`이 `parse_run_request()`로 실행 요청을 만든다.
2. `main`이 앱을 조립한다.
3. `main`이 `SqlRunRequest`를 보고 `SqlRunner`를 생성한다.
4. `CliRunner`가 `run_sql_cli()`를 호출한다.
5. 여러 줄 입력은 세미콜론이 나올 때까지 누적한다.
6. 문장이 완성되면 `SqlInput`으로 감싼다.
7. `SqlSession`에 실행을 위임한다.

### 7.2 파일 입력

1. `main`이 `parse_run_request()`로 파일 실행 요청을 만든다.
2. `main`이 `FileRunner`를 생성한다.
3. `FileRunner`가 파일 목록을 순서대로 읽는다.
4. 파일 전체 내용을 `SqlInput`으로 감싼다.
5. `SqlSession`에 실행을 위임한다.

### 7.3 공통 실행 흐름

1. `SqlRunner`가 입력 방식을 실행한다.
2. `SqlSession`이 입력을 검증한다.
3. `SqlFrontend`가 토큰화/파싱을 수행한다.
4. `StatementList(AST)`가 생성된다.
5. `SqlExecutor`가 각 문장을 순서대로 실행한다.
6. 각 statement 실행 파일이 `catalog`, `storage`, `result`를 사용한다.

---

## 8. statement 실행 구조

각 SQL 문장은 개별 파일로 분리한다.

### `create_table_statement.c`

역할:

- `CreateTableStatement`를 `CatalogSchema`로 변환
- `storage_engine_create_table()` 호출

### `insert_statement.c`

역할:

- 스키마 로드
- 컬럼 정렬
- 행 데이터 준비
- `storage_engine_append_row()` 호출

### `select_statement.c`

역할:

- 스키마 로드
- projection 계산
- WHERE 컬럼 위치 계산
- `storage_engine_scan_rows()` 호출
- 결과 집합 구성
- formatter 출력

### `delete_statement.c`

역할:

- 스키마 로드
- WHERE 비교 컬럼 계산
- `storage_engine_delete_rows()` 호출

### `drop_table_statement.c`

역할:

- `storage_engine_drop_table()` 호출

---

## 9. 저장 포맷 계획

### 9.1 현재 단계

- `.schema`: 커스텀 텍스트 포맷
- `.data`: CSV 포맷

이 조합을 쓰는 이유:

- 사람이 바로 열어볼 수 있다
- 디버깅이 쉽다
- 과제 발표에 유리하다
- 구현 난도가 적절하다

### 9.2 다음 단계 확장 전략

`StorageEngineKind`를 기준으로 구현체를 분리한다.

- `STORAGE_ENGINE_CSV`
- `STORAGE_ENGINE_BINARY`
- `STORAGE_ENGINE_BPTREE`
- `STORAGE_ENGINE_REMOTE`

현재는 CSV만 실제 구현한다.
나머지는 "구조만 열어두고 오류 메시지로 미구현을 알린다."

즉, 지금 단계의 목표는
"실제 SQL DB와 동일한 파일 포맷"
이 아니라
"저장 형식을 갈아끼울 수 있는 인터페이스"
를 먼저 완성하는 것이다.

---

## 10. 단계별 구현 계획

## 10.1 1단계: 구조 정리

목표:

- `app / session / frontend / executor / catalog / storage / result / common`
  구조 고정
- 오래된 파일명 제거
- 죽은 코드 제거

완료 조건:

- 동일 책임이 여러 파일에 섞여 있지 않다
- 이전 구조의 남은 미사용 파일이 없다

## 10.2 2단계: 상위 흐름 단순화

목표:

- 각 파일의 대표 함수가 "읽히는 코드"가 되게 한다

예시:

- `main()`
  - 실행 명령 파싱
  - 앱 생성
  - 실행기 생성
  - 실행
  - 종료
- `parse_run_request()`
  - 기본값 설정
  - 인자 해석
  - 실행 모드 결정
- `sql_session_execute()`
  - 입력 검증
  - 컴파일
  - 실행
  - 오류 문맥 정리
- `sql_frontend_compile()`
  - 요청 검증
  - 토큰화
  - 파싱
  - 토큰 정리

완료 조건:

- 대표 함수 안의 세부 연산이 helper로 내려가 있다
- 대표 함수만 읽어도 흐름을 설명할 수 있다
- `main`이 구체 실행기 타입을 직접 분기하지 않는다

## 10.3 3단계: statement 파일 분리

목표:

- `INSERT/SELECT/CREATE/DROP/DELETE`를 파일 단위로 분리

완료 조건:

- 명령 종류별 실행 코드가 한 파일에 섞여 있지 않다
- 공통 로직은 `sql_statement_support`로 모인다

## 10.4 4단계: 저장 계층 정리

목표:

- 스키마, 경로, CSV, 엔진 구현 책임 분리

완료 조건:

- `schema_catalog`는 스키마만 담당한다
- `storage_path`는 경로만 담당한다
- `csv_codec`는 CSV만 담당한다
- `storage_engine`은 저장 엔진 구현만 담당한다

## 10.5 5단계: 한글화

목표:

- 사용자 메시지, 오류 메시지, 주석을 한글로 통일

완료 조건:

- 사용자에게 직접 보이는 메시지가 영어로 남아 있지 않다
- 주석이 한국어 기준으로 읽힌다

## 10.6 6단계: 테스트 강화

목표:

- 단위 테스트
- 통합 테스트
- CLI 테스트

검증 항목:

- 토큰화
- 다중 문장 파싱
- WHERE
- CREATE/INSERT/SELECT/DELETE/DROP 왕복
- schema-qualified table
- CSV escape
- CLI 인자 검증

## 10.7 7단계: 정리와 발표 준비

목표:

- README와 실제 구조를 일치시킨다
- 데모 명령과 테스트 명령을 정리한다
- 설명 흐름을 통일한다

완료 조건:

- README와 실제 코드 구조가 다르지 않다
- 발표 때 "입력 -> 해석 -> AST -> 실행 -> 저장"을 코드 기준으로 설명 가능하다

---

## 11. 함수 설계 기준

대표 함수는 아래 질문에 답해야 한다.

- 지금 무엇을 검증하는가
- 무엇을 준비하는가
- 무엇을 실행하는가
- 실패하면 어디서 정리하는가

대표 함수 안에 아래 요소가 너무 많으면 helper로 분리한다.

- 메모리 할당 + 해제
- 길고 반복적인 분기
- 같은 에러 처리 패턴
- 파일 경로 계산
- 컬럼 인덱스 계산

하지만 아래 조건이면 굳이 더 쪼개지 않는다.

- 로직이 3~5줄 수준으로 매우 짧다
- 다른 곳에서 재사용되지 않는다
- 오히려 함수가 늘어서 읽기 더 어려워진다

---

## 12. 죽은 코드 정리 기준

다음 항목은 과감히 삭제한다.

- 호출되지 않는 함수
- 더 이상 사용하지 않는 enum 값
- 미사용 필드
- 이전 구조 이름의 호환용 래퍼
- 실패 경로에서 아무 의미 없는 임시 변수
- 중복 cleanup 로직
- `CliOptions`처럼 중간 상태로만 존재하는 중복 명령 구조체

다만 아래는 죽은 코드로 보지 않는다.

- 현재는 미구현이지만 확장 지점으로 의도적으로 남긴 enum
- 인터페이스 경계로 필요한 함수 포인터 슬롯
- 향후 입력 방식 추가를 위한 `SqlInputKind`
- 향후 저장 엔진 교체를 위한 `StorageEngineKind`
- `SqlRunner` 인터페이스와 그 구현 슬롯

---

## 13. 검증 계획

매 리팩터링 단계마다 아래를 반복한다.

1. `make test`
2. `make analyze`
3. `cc -Wall -Wextra -Wpedantic -Wunused-* -fsyntax-only ...`
4. 실제 CLI 실행 확인

실패 시 원칙:

- 기능 변경과 리팩터링을 한 번에 크게 섞지 않는다
- 상위 흐름을 바꾼 뒤 바로 테스트한다
- 미사용 코드 제거 후 다시 분석한다

---

## 14. 완료 정의

아래 항목이 모두 만족되면 이번 단계는 완료다.

- 현재 지원 명령이 모두 동작한다
- 대표 함수만 읽어도 흐름 설명이 가능하다
- `main -> SqlRunRequest -> SqlRunner -> SqlSession` 구조가 코드에 그대로 드러난다
- 계층 책임이 겹치지 않는다
- 한글 주석과 한글 오류 메시지가 적용되어 있다
- 죽은 코드가 남아 있지 않다
- 테스트와 정적 분석이 모두 통과한다
- README와 실제 구조가 일치한다

---

## 15. 이후 확장 계획

현재 구조를 기준으로 다음 기능은 아래 위치에 붙인다.

- 소켓 입력: `src/session/`
- 업데이트 루프 입력: `src/session/`
- 트랜잭션 관리자: `src/executor/` 또는 별도 `src/transaction/`
- 바이너리 저장: `src/storage/`
- B+Tree 저장: `src/storage/`
- 원격 저장: `src/storage/`
- JSON 결과 출력: `src/result/`

즉, 이후 확장은 "기존 코드를 무너뜨리는 방식"이 아니라
"빈 슬롯을 실제 구현으로 채우는 방식"으로 진행해야 한다.

---

## 16. 작업 우선순위 요약

가장 먼저 할 일:

1. 대표 함수 가독성 개선
2. 남은 영문 메시지/주석 한글화
3. 죽은 코드 제거
4. 테스트/분석 통과

그 다음 할 일:

1. 결과 포맷 확장
2. 세션 입력 방식 확장
3. 저장 엔진 구현 추가
4. 트랜잭션 계층 도입

---

## 17. 한 줄 결론

이번 프로젝트의 핵심 목표는
"작동하는 SQL 처리기"를 넘어서,
"입력 -> 해석 -> AST -> 실행 -> 저장"
흐름이 코드 구조에 그대로 드러나는 작은 SQL 엔진을 만드는 것이다.

이 문서에 맞춰 구현을 진행하면
발표, 유지보수, 확장성, 학습 설명력까지 모두 챙길 수 있다.

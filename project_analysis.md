# SQL Processor 프로젝트 전체 분석

## 📌 프로젝트 개요

파일 기반 SQL 처리기 MVP로, SQL 파일을 입력받아 **파싱 → 실행 → CSV 저장/조회**를 수행하는 C 프로그램입니다.
핵심 목표는 DBMS의 전체 흐름(입력 → 파싱 → 실행 → 저장)을 학습용으로 보여주는 것입니다.

---

## 📁 프로젝트 구조

| 파일 | 역할 | 주요 함수 |
|------|------|-----------|
| `main.c` | CLI 진입점, SQL 파일 읽기 | `main()`, `read_sql_file()` |
| `common.h` | 공통 상수, Statement 구조체 정의 | — |
| `parser.h` / `parser.c` | SQL 문자열 → Statement 구조체 변환 | `parse_sql()`, `parse_insert()`, `parse_select()` |
| `executor.h` / `executor.c` | Statement를 보고 storage 함수 호출 | `execute_statement()` |
| `storage.h` / `storage.c` | CSV 파일 읽기/쓰기, 데이터 검증 | `storage_append_row()`, `storage_select_all()` |
| `tests.c` | 단위/통합 테스트 11개 케이스 | `test_*()` 함수들 |
| `Makefile` | 빌드/테스트 자동화 | `make`, `make test`, `make clean` |
| `data/users.csv` | 테이블 데이터, 첫 줄이 스키마 | — |

---

## 🧱 핵심 자료구조: Statement

모든 모듈 사이의 **데이터 전달 매개체**로 사용됩니다. Parser가 채우고, Executor와 Storage가 읽습니다.

```c
typedef struct Statement {
    StatementType type;                                    // INSERT or SELECT
    char table_name[64];                                   // 대상 테이블 이름
    int value_count;                                       // INSERT 값 개수
    char values[16][128];                                  // INSERT 값 배열
    int select_all;                                        // SELECT * 여부 (1 = *, 0 = 컬럼 지정)
    int select_column_count;                               // 선택된 컬럼 수
    char select_columns[16][64];                           // 선택된 컬럼 이름 배열
} Statement;
```

> **핵심**: `Statement`는 이 시스템의 **중간 표현(IR)**입니다. Parser → Executor → Storage 세 계층을 연결하는 유일한 데이터 구조입니다.

---

## 1️⃣ 전체 시스템 아키텍처 다이어그램

```mermaid
graph TB
    subgraph INPUT["사용자 입력"]
        A["SQL 파일"]
    end

    subgraph MAIN["main.c - CLI 진입점"]
        B["argv 검사"]
        C["read_sql_file"]
    end

    subgraph PARSER["parser.c - SQL 파싱"]
        D["parse_sql 전처리"]
        E{"키워드 판별"}
        F["parse_insert"]
        G["parse_select"]
    end

    subgraph COMMON["common.h - 중간 표현"]
        H["Statement 구조체"]
    end

    subgraph EXECUTOR["executor.c - 실행 분기"]
        I["execute_statement"]
        J["INSERT 분기"]
        K["SELECT 분기"]
    end

    subgraph STORAGE["storage.c - CSV 파일 IO"]
        L["storage_append_row"]
        M["storage_select_all"]
    end

    subgraph DATA["데이터 저장소"]
        N[("data/table.csv")]
    end

    subgraph OUTPUT["출력"]
        O["stdout 결과 출력"]
        P["stderr 에러 메시지"]
    end

    A --> B --> C
    C -->|SQL 문자열| D
    D --> E
    E -->|INSERT| F
    E -->|SELECT| G
    E -->|그 외| P
    F -->|Statement 채움| H
    G -->|Statement 채움| H
    H --> I
    I --> J
    I --> K
    J --> L
    K --> M
    L -->|읽기 검증| N
    L -->|쓰기 append| N
    M -->|읽기 조회| N
    L -->|성공| O
    L -->|실패| P
    M -->|결과 출력| O
    M -->|실패| P

    style A fill:#4A90D9,stroke:#2C5F8A,color:#fff
    style H fill:#F5A623,stroke:#D4841A,color:#fff
    style N fill:#7ED321,stroke:#5DA018,color:#fff
    style O fill:#50E3C2,stroke:#35A689,color:#000
    style P fill:#D0021B,stroke:#A5011A,color:#fff
```

### 계층 요약

| 계층 | 파일 | 역할 |
|------|------|------|
| **입력 계층** | `main.c` | CLI 인자 검사, SQL 파일 읽기 |
| **파싱 계층** | `parser.c` | SQL 문자열 → Statement 변환 |
| **실행 계층** | `executor.c` | Statement type 분기, storage 호출 |
| **저장 계층** | `storage.c` | CSV 파일 읽기/쓰기, 데이터 검증 |
| **데이터** | `data/*.csv` | CSV 형식의 테이블 파일 |

---

## 2️⃣ INSERT 흐름 다이어그램

`INSERT INTO users VALUES (1, 'Alice', 20);` 실행 시의 전체 흐름입니다.

```mermaid
sequenceDiagram
    participant User as 사용자
    participant Main as main.c
    participant Parser as parser.c
    participant Executor as executor.c
    participant Storage as storage.c
    participant CSV as data/users.csv

    User->>Main: sql_processor insert_user.sql

    Note over Main: 1. argv 검사
    Main->>Main: read_sql_file 실행

    Main->>Parser: parse_sql 호출

    Note over Parser: 2. 전처리
    Parser->>Parser: trim_in_place 실행
    Parser->>Parser: 세미콜론 존재 확인
    Parser->>Parser: 세미콜론 2개 이상이면 에러
    Parser->>Parser: 세미콜론 제거 후 trim

    Note over Parser: 3. 키워드 판별
    Parser->>Parser: starts_with_keyword INSERT = true

    Note over Parser: 4. parse_insert 실행
    Parser->>Parser: consume_keyword INSERT 성공
    Parser->>Parser: consume_keyword INTO 성공
    Parser->>Parser: parse_identifier = users
    Parser->>Parser: consume_keyword VALUES 성공
    Parser->>Parser: parse_values_list 실행

    Note over Parser: 5. VALUES 파싱
    Parser->>Parser: 여는 괄호 확인
    Parser->>Parser: 1 = parse_unquoted_value
    Parser->>Parser: Alice = parse_quoted_value
    Parser->>Parser: 20 = parse_unquoted_value
    Parser->>Parser: 닫는 괄호 확인

    Parser-->>Main: Statement 반환 type=INSERT table=users values=1,Alice,20

    Main->>Executor: execute_statement 호출

    Note over Executor: 6. type 분기
    Executor->>Executor: switch STATEMENT_INSERT

    Executor->>Storage: storage_append_row 호출

    Note over Storage: 7. 경로 생성
    Storage->>Storage: build_table_path = data/users.csv

    Note over Storage: 8. 헤더 검증
    Storage->>CSV: fopen으로 읽기 모드 열기
    CSV-->>Storage: 파일 핸들
    Storage->>CSV: fgets로 헤더 읽기 = id,name,age
    Storage->>Storage: split_csv_line = 3개 컬럼
    Storage->>Storage: header_columns 3 == value_count 3 통과

    Note over Storage: 9. ID 중복 검사
    Storage->>Storage: find_column_index id = index 0
    Storage->>CSV: fgets 반복하며 기존 행 읽기
    Storage->>Storage: 각 행의 id값과 1 비교
    Note over Storage: 중복 없음 = 통과

    Storage->>CSV: fclose

    Note over Storage: 10. 데이터 기록
    Storage->>CSV: fopen append 모드
    Storage->>CSV: 1,Alice,20 기록
    Storage->>CSV: 줄바꿈 + fclose

    Storage-->>Executor: return 1 성공
    Executor->>User: Inserted 1 row into users 출력
```

### INSERT 세부 흐름 — 검증 로직

```mermaid
flowchart TD
    A["storage_append_row 시작"] --> B["build_table_path 경로 생성"]
    B --> C{"파일이 존재하는가?"}
    C -->|No| ERR1["Table file not found"]
    C -->|Yes| D["fgets로 헤더 행 읽기"]
    D --> E{"헤더가 비어있는가?"}
    E -->|Yes| ERR2["Table file is empty"]
    E -->|No| F["split_csv_line 헤더 컬럼 분리"]
    F --> G{"header_columns == value_count?"}
    G -->|No| ERR3["Column count mismatch"]
    G -->|Yes| H["find_column_index id"]
    H --> I{"id 컬럼이 존재하는가?"}
    I -->|No| K["중복 검사 생략"]
    I -->|Yes| J["모든 기존 행 순회하며 id 값 비교"]
    J --> J1{"중복 id 발견?"}
    J1 -->|Yes| ERR4["Duplicate id value"]
    J1 -->|No| K
    K --> L["fclose 읽기 모드 닫기"]
    L --> M["fopen append 모드 열기"]
    M --> N["값들을 CSV 형식으로 쉼표 구분 기록"]
    N --> O["줄바꿈 + fclose"]
    O --> P["return 1 성공"]

    style ERR1 fill:#D0021B,stroke:#A5011A,color:#fff
    style ERR2 fill:#D0021B,stroke:#A5011A,color:#fff
    style ERR3 fill:#D0021B,stroke:#A5011A,color:#fff
    style ERR4 fill:#D0021B,stroke:#A5011A,color:#fff
    style P fill:#7ED321,stroke:#5DA018,color:#fff
```

---

## 3️⃣ SELECT 흐름 다이어그램

### SELECT * 흐름

`SELECT * FROM users;` 실행 시의 전체 흐름입니다.

```mermaid
sequenceDiagram
    participant User as 사용자
    participant Main as main.c
    participant Parser as parser.c
    participant Executor as executor.c
    participant Storage as storage.c
    participant CSV as data/users.csv

    User->>Main: sql_processor select_users.sql

    Main->>Main: read_sql_file 실행

    Main->>Parser: parse_sql 호출

    Note over Parser: 1. 전처리 trim 세미콜론 검증
    Note over Parser: 2. starts_with_keyword SELECT = true

    Note over Parser: 3. parse_select 실행
    Parser->>Parser: consume_keyword SELECT 성공
    Parser->>Parser: skip_spaces
    Parser->>Parser: cursor가 별표 = select_all = 1
    Parser->>Parser: consume_keyword FROM 성공
    Parser->>Parser: parse_identifier = users

    Parser-->>Main: Statement 반환 type=SELECT table=users select_all=1

    Main->>Executor: execute_statement 호출
    Executor->>Executor: switch STATEMENT_SELECT
    Executor->>Storage: storage_select_all 호출

    Note over Storage: 4. 파일 열기 및 헤더 파싱
    Storage->>CSV: fopen 읽기 모드
    Storage->>CSV: fgets 헤더 = id,name,age
    Storage->>Storage: split_csv_line = id, name, age

    Note over Storage: 5. select_all == 1이므로 selected_indexes = 0,1,2

    Storage->>User: write_selected_fields 출력 id,name,age

    Note over Storage: 6. 데이터 행 순회
    Storage->>CSV: fgets = 1,Alice,20
    Storage->>Storage: split_csv_line = 1, Alice, 20
    Storage->>User: write_selected_fields 출력 1,Alice,20

    Storage->>CSV: fgets = 2,hoseok,24
    Storage->>Storage: split_csv_line = 2, hoseok, 24
    Storage->>User: write_selected_fields 출력 2,hoseok,24

    Storage->>CSV: fgets = NULL EOF
    Storage->>CSV: fclose

    Storage->>User: Rows 2 출력
```

### SELECT 컬럼 지정 흐름

`SELECT name, id FROM users;` — 컬럼 프로젝션(Column Projection) 동작 방식

```mermaid
flowchart TD
    A["storage_select_all 시작"] --> B["build_table_path = data/users.csv"]
    B --> C{"파일이 존재하는가?"}
    C -->|No| ERR1["Table file not found"]
    C -->|Yes| D["fgets 헤더 행 읽기"]
    D --> E["split_csv_line = id, name, age"]

    E --> F{"select_all 인가?"}

    F -->|"Yes SELECT 전체"| G["selected_indexes = 0, 1, 2"]
    F -->|"No 컬럼 지정"| H["select_columns에서 각 컬럼을 header에서 찾기"]

    H --> H1["find_column_index name = 1"]
    H1 --> H2["find_column_index id = 0"]
    H2 --> H3{"모든 컬럼을 찾았는가?"}
    H3 -->|No| ERR2["Unknown column in SELECT"]
    H3 -->|Yes| I["selected_indexes = 1, 0"]

    G --> J["헤더 출력 write_selected_fields"]
    I --> J

    J --> K["데이터 행 반복 읽기"]
    K --> L{"fgets 성공?"}
    L -->|Yes| M["split_csv_line으로 행 분리"]
    M --> N["selected_indexes 순서로 필드 출력"]
    N --> O["row_count 증가"]
    O --> L
    L -->|"No EOF"| Q["fclose"]
    Q --> R["Rows row_count 출력"]
    R --> S["return 1 성공"]

    style ERR1 fill:#D0021B,stroke:#A5011A,color:#fff
    style ERR2 fill:#D0021B,stroke:#A5011A,color:#fff
    style S fill:#7ED321,stroke:#5DA018,color:#fff
```

### 컬럼 프로젝션 예시

```
헤더:  id(0), name(1), age(2)
요청:  SELECT name, id FROM users;

selected_indexes = [1, 0]

CSV 행: 1,Alice,20
        ↓ index 1 → "Alice"
        ↓ index 0 → "1"
출력:   Alice,1
```

---

## 🔄 모듈 간 의존 관계

```mermaid
graph LR
    subgraph HEADERS["헤더 - 인터페이스"]
        CH["common.h"]
        PH["parser.h"]
        EH["executor.h"]
        SH["storage.h"]
    end

    subgraph SOURCES["소스 - 구현"]
        MC["main.c"]
        PC["parser.c"]
        EC["executor.c"]
        SC["storage.c"]
        TC["tests.c"]
    end

    subgraph EXTERNAL["외부"]
        CSV[("data/*.csv")]
    end

    MC -->|include| CH
    MC -->|include| PH
    MC -->|include| EH
    PC -->|include| PH
    PH -->|include| CH
    EC -->|include| EH
    EC -->|include| SH
    EH -->|include| CH
    SC -->|include| SH
    SH -->|include| CH
    TC -->|include| CH
    TC -->|include| PH
    TC -->|include| EH

    MC -->|호출| PC
    MC -->|호출| EC
    EC -->|호출| SC
    SC -->|읽기 쓰기| CSV

    style CH fill:#F5A623,stroke:#D4841A,color:#fff
    style CSV fill:#7ED321,stroke:#5DA018,color:#fff
```

> **단방향 의존**: `main.c` → `parser.c` → (없음), `main.c` → `executor.c` → `storage.c` → CSV.
> Parser와 Storage는 서로 모르며, Executor가 둘을 연결합니다.

---

## 🧪 테스트 구조

| # | 테스트 함수 | 검증 대상 | 계층 |
|---|-----------|----------|------|
| 1 | `test_parse_insert_case_insensitive` | 소문자 insert 파싱 | Parser |
| 2 | `test_parse_select_specific_columns` | 컬럼 지정 SELECT 파싱 | Parser |
| 3 | `test_insert_and_select_flow` | INSERT → SELECT 통합 흐름 | 전체 |
| 4 | `test_select_specific_columns_flow` | 컬럼 프로젝션 통합 흐름 | 전체 |
| 5 | `test_invalid_insert_syntax` | INSERT users VALUES 에러 | Parser |
| 6 | `test_invalid_select_syntax` | SELECT users 에러 | Parser |
| 7 | `test_missing_semicolon` | 세미콜론 누락 에러 | Parser |
| 8 | `test_missing_table_file` | 존재하지 않는 테이블 파일 | Storage |
| 9 | `test_empty_sql_input` | 빈 SQL 입력 | Parser |
| 10 | `test_column_count_mismatch` | 헤더/값 개수 불일치 | Storage |
| 11 | `test_duplicate_id_insert` | ID 중복 검사 | Storage |

---

## 📊 에러 처리 흐름

모든 계층에서 int 반환값(1=성공, 0=실패)과 char error 버퍼를 사용한 **일관된 에러 전파** 패턴:

```mermaid
flowchart LR
    A["storage.c set_error + return 0"] --> B["executor.c return 0 전파"]
    B --> C["main.c stderr에 error 출력"]
    C --> D["exit 1"]

    style D fill:#D0021B,stroke:#A5011A,color:#fff
```

각 함수는 실패 시 error 버퍼에 메시지를 쓰고 return 0을 하며, 호출자가 이를 체크하여 상위로 전파합니다. 최종적으로 main.c가 stderr에 출력하고 종료합니다.

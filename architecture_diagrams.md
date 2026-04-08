# SQL Processor Diagrams

이 문서는 현재 코드 기준으로 SQL Processor의 핵심 흐름을 다이어그램으로 정리한 것입니다.
기준 파일은 [main.c](/Users/juhoseok/Desktop/sql_processor/main.c), [parser.c](/Users/juhoseok/Desktop/sql_processor/parser.c), [executor.c](/Users/juhoseok/Desktop/sql_processor/executor.c), [storage.c](/Users/juhoseok/Desktop/sql_processor/storage.c) 입니다.

## 1. 전체 시스템 다이어그램

```mermaid
flowchart LR
    U["SQL 파일"]
    M["main.c\nread_sql_file()"]
    P["parser.c\nparse_sql()"]
    S["Statement\n(common.h)"]
    E["executor.c\nexecute_statement()"]
    I["storage_append_row()"]
    Q["storage_select_all()"]
    C[("data/<table>.csv")]
    O["stdout / stderr"]

    U --> M
    M -->|SQL 문자열| P
    P -->|INSERT 또는 SELECT로 파싱| S
    S --> E
    E -->|STATEMENT_INSERT| I
    E -->|STATEMENT_SELECT| Q
    I -->|헤더 검증 / 중복 검사 / append| C
    Q -->|헤더 파싱 / 행 순회 / 투영| C
    I --> O
    Q --> O
    M -->|파일 읽기 실패| O
    P -->|구문 오류| O
```

핵심 구조:

- `main.c`는 파일을 읽고 파싱과 실행을 연결합니다.
- `parser.c`는 SQL을 `Statement`라는 중간 표현으로 바꿉니다.
- `executor.c`는 `Statement.type`으로 `INSERT`와 `SELECT`를 분기합니다.
- `storage.c`는 실제 CSV 파일 입출력과 검증을 담당합니다.

## 2. INSERT 다이어그램

예시 SQL: `INSERT INTO users VALUES (1, 'Alice', 20);`

```mermaid
sequenceDiagram
    participant User as SQL File
    participant Main as main.c
    participant Parser as parser.c
    participant Executor as executor.c
    participant Storage as storage.c
    participant CSV as data/users.csv

    User->>Main: 파일 경로 전달
    Main->>Main: read_sql_file()
    Main->>Parser: parse_sql(sql, &stmt, error)

    Parser->>Parser: trim + 세미콜론 검증
    Parser->>Parser: starts_with_keyword("INSERT")
    Parser->>Parser: parse_insert()
    Parser->>Parser: table_name = users
    Parser->>Parser: values = [1, Alice, 20]
    Parser-->>Main: Statement { type=INSERT }

    Main->>Executor: execute_statement(&stmt, "data", stdout, error)
    Executor->>Storage: storage_append_row("data", &stmt, error)

    Storage->>Storage: build_table_path("data/users.csv")
    Storage->>CSV: fopen(..., "r")
    Storage->>CSV: 헤더 읽기
    Storage->>Storage: 헤더 컬럼 수와 value_count 비교
    Storage->>Storage: id 컬럼 존재 시 중복 검사
    Storage->>CSV: fclose()
    Storage->>CSV: fopen(..., "a")
    Storage->>CSV: 1,Alice,20 추가
    Storage->>CSV: fclose()
    Storage-->>Executor: 성공

    Executor-->>Main: "Inserted 1 row into users"
    Main-->>User: stdout 출력
```

INSERT에서 중요한 검증:

- 테이블 파일이 없으면 실패합니다.
- 헤더 컬럼 수와 `VALUES` 개수가 다르면 실패합니다.
- 헤더에 `id` 컬럼이 있으면 기존 모든 행을 읽어 중복을 검사합니다.

## 3. SELECT 다이어그램

예시 SQL:

- `SELECT * FROM users;`
- `SELECT name, id FROM users;`

```mermaid
flowchart TD
    A["SQL 파일 읽기\nmain.c"] --> B["parse_sql()\nparser.c"]
    B --> C{"SELECT * 인가?"}
    C -->|Yes| D["stmt.select_all = 1"]
    C -->|No| E["select_columns[] 채우기"]
    D --> F["table_name 파싱"]
    E --> F
    F --> G["execute_statement()\nexecutor.c"]
    G --> H["storage_select_all()\nstorage.c"]
    H --> I["data/<table>.csv 열기"]
    I --> J["헤더 split_csv_line()"]
    J --> K{"stmt.select_all ?"}
    K -->|Yes| L["selected_indexes = 0..header_columns-1"]
    K -->|No| M["요청 컬럼을 header에서 찾아 index 매핑"]
    M --> N{"모든 컬럼 존재?"}
    N -->|No| X["Unknown column in SELECT 에러"]
    N -->|Yes| O["selected_indexes 확정"]
    L --> P["헤더 출력"]
    O --> P
    P --> Q["데이터 행 반복 읽기"]
    Q --> R["row를 split_csv_line()"]
    R --> S["selected_indexes 순서대로 출력"]
    S --> T["row_count 증가"]
    T --> U{"다음 행 존재?"}
    U -->|Yes| Q
    U -->|No| V["Rows: N 출력"]
```

SELECT에서 중요한 포인트:

- 실제 조회는 모두 `storage_select_all()` 하나가 담당합니다.
- `SELECT *`면 헤더의 모든 컬럼 인덱스를 그대로 사용합니다.
- 컬럼 지정 조회면 `find_column_index()`로 헤더에서 위치를 찾아 원하는 순서대로 출력합니다.
- 마지막에 항상 `Rows: N`을 출력합니다.

#ifndef MINI_SQL_H
#define MINI_SQL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * 모든 모듈이 공유하는 공통 에러 버퍼 크기다.
 * 각 함수는 이 버퍼에 사람이 읽을 수 있는 오류 메시지를 채운 뒤 false 를 반환한다.
 */
#define MSQL_ERROR_SIZE 512

/*
 * ErrorContext 는 에러 메시지를 담는 단일 구조체다.
 * 기존의 (char *error_buf, size_t error_size) 두 파라미터를 하나로 통합해
 * 함수 시그니처를 간결하게 만든다.
 *
 * 선언: ErrorContext err = {0};
 * 조회: err.buf
 */
typedef struct ErrorContext {
    char buf[MSQL_ERROR_SIZE];
} ErrorContext;

/*
 * lexer 가 SQL 문자열을 잘게 나눈 뒤 만들어내는 토큰 종류다.
 * parser 는 이 enum 값을 기준으로 "지금 읽은 단어가 명령어인지, 식별자인지, 구두점인지"를 판단한다.
 */
typedef enum TokenType {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_CREATE,
    TOKEN_TABLE,
    TOKEN_DROP,
    TOKEN_DELETE,
    TOKEN_STAR,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_EQUALS,
    TOKEN_DOT,
    TOKEN_EOF
} TokenType;

/* SQL 원문에서 잘라낸 최소 단위 하나를 표현한다. */
typedef struct Token {
    TokenType type;
    char *text;
    int line;
    int column;
} Token;

/* 동적으로 늘어나는 토큰 배열이다. */
typedef struct TokenList {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

/*
 * parser 가 SQL 문장을 해석한 뒤 "이 문장이 어떤 종류의 작업인가"를 표시하는 enum 이다.
 * statement_executor 는 이 값을 보고 실제 실행 함수를 고른다.
 */
typedef enum StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_CREATE_TABLE,
    STATEMENT_DROP_TABLE,
    STATEMENT_DELETE
} StatementType;

/* INSERT 문장의 의미 정보를 담는 AST 노드다. */
typedef struct InsertStatement {
    char *table_name;
    char **columns;
    size_t column_count;
    char **values;
    size_t value_count;
} InsertStatement;

/*
 * SELECT/DELETE 가 공유하는 WHERE 조건이다.
 * has_where 가 false 면 나머지 두 필드는 NULL 이다.
 */
typedef struct WhereClause {
    bool has_where;
    char *where_column;
    char *where_value;
} WhereClause;

/* SELECT 문장의 의미 정보를 담는 AST 노드다. */
typedef struct SelectStatement {
    bool select_all;
    char **columns;
    size_t column_count;
    char *table_name;
    WhereClause where;
} SelectStatement;

/* CREATE TABLE 문장의 의미 정보를 담는 AST 노드다. */
typedef struct CreateTableStatement {
    char *table_name;
    char **columns;
    char **column_types;
    size_t column_count;
} CreateTableStatement;

/* DROP TABLE 문장의 의미 정보를 담는 AST 노드다. */
typedef struct DropTableStatement {
    char *table_name;
} DropTableStatement;

/* DELETE 문장의 의미 정보를 담는 AST 노드다. */
typedef struct DeleteStatement {
    char *table_name;
    WhereClause where;
} DeleteStatement;

/*
 * parser 가 만들어내는 "SQL 문장 하나"다.
 * type 은 문장 종류를 나타내고, union 의 실제 사용 멤버는 type 과 1:1 로 대응된다.
 */
typedef struct Statement {
    StatementType type;
    union {
        InsertStatement insert_stmt;
        SelectStatement select_stmt;
        CreateTableStatement create_table_stmt;
        DropTableStatement drop_table_stmt;
        DeleteStatement delete_stmt;
    } as;
} Statement;

/* 여러 SQL 문장을 한 번에 처리할 수 있도록 만든 동적 배열이다. */
typedef struct StatementList {
    Statement *items;
    size_t count;
    size_t capacity;
} StatementList;

typedef struct StorageEngine StorageEngine;
typedef struct SqlApp SqlApp;
typedef struct SqlSession SqlSession;
typedef struct ResultFormatter ResultFormatter;

/*
 * 입력이 어디에서 들어왔는지 나타내는 분류다.
 * 현재는 CLI 와 SQL 파일만 실제로 사용하고,
 * SOCKET / UPDATE_LOOP 는 이후 확장을 위한 자리만 남겨둔다.
 */
typedef enum SqlInputKind {
    SQL_INPUT_CLI,
    SQL_INPUT_FILE,
    SQL_INPUT_SOCKET,
    SQL_INPUT_UPDATE_LOOP
} SqlInputKind;

/*
 * 저장 엔진 종류다.
 * 현재 구현은 CSV 기반 파일 저장만 지원하고,
 * 나머지는 추후 교체 가능한 슬롯으로 남겨둔다.
 */
typedef enum StorageEngineKind {
    STORAGE_ENGINE_CSV,
    STORAGE_ENGINE_BINARY,
    STORAGE_ENGINE_BPTREE,
    STORAGE_ENGINE_REMOTE
} StorageEngineKind;

/*
 * 세션이 처리하는 입력 단위다.
 * 실제 실행에 필요한 SQL 텍스트와, 그 입력의 출처 메타데이터를 함께 가진다.
 */
typedef struct SqlInput {
    SqlInputKind kind;
    const char *source_name;
    const char *text;
} SqlInput;

/*
 * 실행기에 공통으로 전달되는 실행 문맥이다.
 * SQL 계층은 "어디에 저장할지"를 직접 모르고, 이 context 를 통해 저장 엔진과 출력 스트림만 받는다.
 */
typedef struct ExecutionContext {
    StorageEngine *storage_engine;
    FILE *output;
    const ResultFormatter *formatter;
} ExecutionContext;

/* 앱 생성 시 필요한 최소 설정이다. */
typedef struct SqlAppConfig {
    StorageEngineKind storage_kind;
    const char *db_path;
    FILE *output;
    const ResultFormatter *formatter;
} SqlAppConfig;

/* SQL 문자열을 TokenList 로 변환한다. */
bool tokenize_sql(const char *input, TokenList *out_tokens, ErrorContext *err);
/* tokenize_sql 이 할당한 메모리를 해제한다. */
void free_token_list(TokenList *tokens);

/* TokenList 를 StatementList(AST) 로 변환한다. */
bool parse_tokens(const TokenList *tokens, StatementList *out_statements, ErrorContext *err);
/* parse_tokens 가 할당한 AST 메모리를 해제한다. */
void free_statement_list(StatementList *statements);

/* 앱 객체를 만들고, 내부적으로 세션/프런트엔드/실행기/저장 엔진을 조립한다. */
SqlApp *sql_app_create(const SqlAppConfig *config, ErrorContext *err);
/* 앱이 소유한 모든 리소스를 정리한다. */
void sql_app_destroy(SqlApp *app);
/* 앱이 소유한 SQL 세션을 반환한다. */
SqlSession *sql_app_session(SqlApp *app);

/* SQL 세션이 입력 하나를 해석하고 실행한다. */
bool sql_session_execute(SqlSession *session, const SqlInput *input, ErrorContext *err);
/* SQL 파일 전체를 읽어 하나의 요청으로 실행한다. */
bool sql_session_execute_file(SqlSession *session, const char *path, ErrorContext *err);

/* 공통 유틸리티 함수들이다. */
char *msql_strdup(const char *text);
/* text 를 복사해 동적 문자열 배열에 추가한다. 용량이 부족하면 2배로 늘린다. */
bool msql_string_array_push(char ***items, size_t *count, size_t *capacity,
                            const char *text, ErrorContext *err);
/* 이미 소유권을 가진 text 를 동적 문자열 배열에 추가한다. 실패 시 text 를 free 한다. */
bool msql_string_array_push_owned(char ***items, size_t *count, size_t *capacity,
                                  char *text, ErrorContext *err);
char *read_stream_line(FILE *stream);
void set_error(ErrorContext *err, const char *fmt, ...);
bool read_file_all(const char *path, char **out_contents, ErrorContext *err);
void free_string_array(char **items, size_t count);
int find_column_index(char **columns, size_t column_count, const char *target);
bool strings_equal_ci(const char *left, const char *right);

#endif

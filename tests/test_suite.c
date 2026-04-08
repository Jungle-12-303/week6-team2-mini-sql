#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#include "mini_sql.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * test_suite.c 는 함수 수준과 통합 흐름을 함께 검증한다.
 * 각 테스트는 "토큰화 -> 파싱 -> 실행 -> 파일 반영" 중 어디가 깨졌는지 빠르게 찾기 쉽게
 * 작고 명확한 책임 단위로 나뉘어 있다.
 */

static int g_failures = 0;
static int g_tests = 0;

#define ASSERT_TRUE(expr) do { \
    g_tests += 1; \
    if (!(expr)) { \
        fprintf(stderr, "검증 실패: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

#define ASSERT_STREQ(actual, expected) do { \
    g_tests += 1; \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "문자열 비교 실패: %s:%d\n기대값: %s\n실제값: %s\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return false; \
    } \
} while (0)

static bool contains_text(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/* 테스트용 텍스트 파일을 빠르게 만들기 위한 보조 함수다. */
static bool write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return false;
    }

    fputs(contents, file);
    fclose(file);
    return true;
}

/* tmpfile() 스트림 전체를 다시 읽어 문자열로 돌려준다. */
static char *read_temp_stream_all(FILE *stream) {
    long size;
    size_t read_size;
    char *buffer;

    if (fflush(stream) != 0) {
        return NULL;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        return NULL;
    }

    size = ftell(stream);
    if (size < 0L) {
        return NULL;
    }

    rewind(stream);

    buffer = malloc((size_t) size + 1U);
    if (buffer == NULL) {
        return NULL;
    }

    read_size = fread(buffer, 1U, (size_t) size, stream);
    buffer[read_size] = '\0';
    return buffer;
}

/*
 * 앱을 만들어 SQL 을 실행하고, stdout 대신 임시 스트림에 결과를 받아오는 보조 함수다.
 * 통합 테스트는 이 함수를 통해 실제 실행 결과 문자열을 검증한다.
 */
static bool run_sql_capture(const char *db_path, const char *sql, char **out_output, ErrorContext *err) {
    FILE *stream = tmpfile();
    SqlAppConfig config;
    SqlApp *app;
    SqlSession *session;
    SqlInput input;
    bool ok;

    if (stream == NULL) {
        set_error(err, "임시 출력 스트림을 만들지 못했습니다");
        return false;
    }

    config.storage_kind = STORAGE_ENGINE_CSV;
    config.db_path = db_path;
    config.output = stream;
    config.formatter = NULL;
    app = sql_app_create(&config, err);
    if (app == NULL) {
        fclose(stream);
        return false;
    }

    session = sql_app_session(app);
    input.kind = SQL_INPUT_FILE;
    input.source_name = "테스트";
    input.text = sql;
    ok = sql_session_execute(session, &input, err);
    if (!ok) {
        sql_app_destroy(app);
        fclose(stream);
        return false;
    }

    *out_output = read_temp_stream_all(stream);
    sql_app_destroy(app);
    fclose(stream);

    if (*out_output == NULL) {
        set_error(err, "임시 출력 스트림을 읽지 못했습니다");
        return false;
    }

    return true;
}

static void remove_test_dir(const char *dir) {
    char path[512];

    snprintf(path, sizeof(path), "%s/users.schema", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/users.data", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/analytics/events.schema", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/analytics/events.data", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/analytics", dir);
    rmdir(path);
    rmdir(dir);
}

static bool test_tokenizer_handles_comments_and_strings(void) {
    TokenList tokens = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users (name, age) VALUES ('O''Reilly', 20); -- comment\nSELECT * FROM users;",
        &tokens,
        &err
    ));
    ASSERT_TRUE(tokens.count > 0U);
    ASSERT_TRUE(tokens.items[0].type == TOKEN_INSERT);
    ASSERT_STREQ(tokens.items[10].text, "O'Reilly");
    ASSERT_TRUE(tokens.items[tokens.count - 1U].type == TOKEN_EOF);

    free_token_list(&tokens);
    return true;
}

static bool test_parser_supports_multi_statement_where_clause(void) {
    TokenList tokens = {0};
    StatementList statements = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users VALUES (1, 'Alice', 20); SELECT id, name FROM users WHERE age = 20;",
        &tokens,
        &err
    ));
    ASSERT_TRUE(parse_tokens(&tokens, &statements, &err));
    ASSERT_TRUE(statements.count == 2U);
    ASSERT_TRUE(statements.items[0].type == STATEMENT_INSERT);
    ASSERT_TRUE(statements.items[1].type == STATEMENT_SELECT);
    ASSERT_STREQ(statements.items[1].as.select_stmt.where.where_column, "age");
    ASSERT_STREQ(statements.items[1].as.select_stmt.where.where_value, "20");

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_parser_supports_create_delete_and_drop(void) {
    TokenList tokens = {0};
    StatementList statements = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "CREATE TABLE users (id INT, name TEXT); DELETE FROM users WHERE id = 1; DROP TABLE users;",
        &tokens,
        &err
    ));
    ASSERT_TRUE(parse_tokens(&tokens, &statements, &err));
    ASSERT_TRUE(statements.count == 3U);
    ASSERT_TRUE(statements.items[0].type == STATEMENT_CREATE_TABLE);
    ASSERT_TRUE(statements.items[1].type == STATEMENT_DELETE);
    ASSERT_TRUE(statements.items[2].type == STATEMENT_DROP_TABLE);
    ASSERT_STREQ(statements.items[0].as.create_table_stmt.columns[0], "id");
    ASSERT_STREQ(statements.items[0].as.create_table_stmt.column_types[0], "INT");
    ASSERT_STREQ(statements.items[1].as.delete_stmt.where.where_column, "id");

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_parser_supports_schema_options(void) {
    TokenList tokens = {0};
    StatementList statements = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(20), note TEXT(50));",
        &tokens,
        &err
    ));
    ASSERT_TRUE(parse_tokens(&tokens, &statements, &err));
    ASSERT_TRUE(statements.count == 1U);
    ASSERT_TRUE(statements.items[0].type == STATEMENT_CREATE_TABLE);
    ASSERT_TRUE(statements.items[0].as.create_table_stmt.column_is_primary_keys[0]);
    ASSERT_TRUE(statements.items[0].as.create_table_stmt.column_sizes[1] == 20U);
    ASSERT_TRUE(statements.items[0].as.create_table_stmt.column_sizes[2] == 50U);
    ASSERT_STREQ(statements.items[0].as.create_table_stmt.column_types[1], "VARCHAR");

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_parser_supports_insert_rows_and_select_limit(void) {
    TokenList tokens = {0};
    StatementList statements = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users VALUES (1, 'Alice', 20), (2, 'Bob', 21);"
        "SELECT TOP 1 name FROM users ORDER BY age DESC LIMIT 1;",
        &tokens,
        &err
    ));
    ASSERT_TRUE(!parse_tokens(&tokens, &statements, &err));
    ASSERT_TRUE(contains_text(err.buf, "TOP과 LIMIT는 함께 사용할 수 없습니다"));

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_parser_supports_top_and_order_by(void) {
    TokenList tokens = {0};
    StatementList statements = {0};
    ErrorContext err = {0};

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users VALUES (1, 'Alice', 20), (2, 'Bob', 21);"
        "SELECT TOP 1 name FROM users ORDER BY age DESC;",
        &tokens,
        &err
    ));
    ASSERT_TRUE(parse_tokens(&tokens, &statements, &err));
    ASSERT_TRUE(statements.count == 2U);
    ASSERT_TRUE(statements.items[0].type == STATEMENT_INSERT);
    ASSERT_TRUE(statements.items[0].as.insert_stmt.row_count == 2U);
    ASSERT_TRUE(statements.items[0].as.insert_stmt.value_count == 3U);
    ASSERT_TRUE(statements.items[1].type == STATEMENT_SELECT);
    ASSERT_TRUE(statements.items[1].as.select_stmt.has_row_limit);
    ASSERT_TRUE(statements.items[1].as.select_stmt.row_limit == 1U);
    ASSERT_TRUE(statements.items[1].as.select_stmt.order_by.has_order_by);
    ASSERT_STREQ(statements.items[1].as.select_stmt.order_by.column, "age");
    ASSERT_TRUE(statements.items[1].as.select_stmt.order_by.descending);

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_insert_and_select_round_trip(void) {
    char template[] = "/tmp/mini_sql_test_XXXXXX";
    char *dir = mkdtemp(template);
    char schema_path[512];
    char data_path[512];
    ErrorContext err = {0};
    char *output = NULL;
    char *data_contents = NULL;

    ASSERT_TRUE(dir != NULL);

    snprintf(schema_path, sizeof(schema_path), "%s/users.schema", dir);
    ASSERT_TRUE(write_text_file(schema_path, "id,name,age\n"));

    ASSERT_TRUE(run_sql_capture(dir,
                                "INSERT INTO users VALUES (1, 'Alice', 20);"
                                "SELECT id, name FROM users WHERE age = 20;",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "INSERT 1"));
    ASSERT_TRUE(contains_text(output, "Alice"));
    ASSERT_TRUE(contains_text(output, "(1개 행)"));

    snprintf(data_path, sizeof(data_path), "%s/users.data", dir);
    ASSERT_TRUE(read_file_all(data_path, &data_contents, &err));
    ASSERT_STREQ(data_contents, "1,Alice,20\n");

    free(output);
    free(data_contents);
    remove_test_dir(dir);
    return true;
}

static bool test_schema_qualified_table_and_csv_escape(void) {
    char template[] = "/tmp/mini_sql_nested_XXXXXX";
    char *dir = mkdtemp(template);
    char analytics_dir[512];
    char schema_path[512];
    ErrorContext err = {0};
    char *output = NULL;

    ASSERT_TRUE(dir != NULL);

    snprintf(analytics_dir, sizeof(analytics_dir), "%s/analytics", dir);
    ASSERT_TRUE(mkdir(analytics_dir, 0755) == 0);

    snprintf(schema_path, sizeof(schema_path), "%s/analytics/events.schema", dir);
    ASSERT_TRUE(write_text_file(schema_path, "id,message\n"));

    ASSERT_TRUE(run_sql_capture(dir,
                                "INSERT INTO analytics.events VALUES (7, 'hello, \"sql\"');"
                                "SELECT * FROM analytics.events WHERE id = 7;",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "hello, \"sql\""));
    ASSERT_TRUE(contains_text(output, "(1개 행)"));

    free(output);
    remove_test_dir(dir);
    return true;
}

static bool test_create_delete_and_drop_round_trip(void) {
    char template[] = "/tmp/mini_sql_ddl_XXXXXX";
    char *dir = mkdtemp(template);
    char schema_path[512];
    char data_path[512];
    ErrorContext err = {0};
    char *output = NULL;

    ASSERT_TRUE(dir != NULL);

    ASSERT_TRUE(run_sql_capture(dir,
                                "CREATE TABLE users (id INT, name TEXT, age INT);"
                                "INSERT INTO users VALUES (1, 'Alice', 20);"
                                "INSERT INTO users VALUES (2, 'Bob', 21);"
                                "DELETE FROM users WHERE age = 20;"
                                "SELECT * FROM users;"
                                "DROP TABLE users;",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "CREATE TABLE"));
    ASSERT_TRUE(contains_text(output, "DELETE 1"));
    ASSERT_TRUE(contains_text(output, "Bob"));
    ASSERT_TRUE(!contains_text(output, "| Alice "));
    ASSERT_TRUE(contains_text(output, "DROP TABLE"));

    snprintf(schema_path, sizeof(schema_path), "%s/users.schema", dir);
    snprintf(data_path, sizeof(data_path), "%s/users.data", dir);
    ASSERT_TRUE(access(schema_path, F_OK) != 0);
    ASSERT_TRUE(access(data_path, F_OK) != 0);

    free(output);
    remove_test_dir(dir);
    return true;
}

static bool test_primary_key_and_length_constraints(void) {
    char template[] = "/tmp/mini_sql_constraints_XXXXXX";
    char *dir = mkdtemp(template);
    ErrorContext err = {0};
    char *output = NULL;

    ASSERT_TRUE(dir != NULL);

    ASSERT_TRUE(run_sql_capture(dir,
                                "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(5), age INT);"
                                "INSERT INTO users VALUES (1, 'Alice', 20);",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "CREATE TABLE"));
    ASSERT_TRUE(contains_text(output, "INSERT 1"));
    free(output);
    output = NULL;

    ASSERT_TRUE(!run_sql_capture(dir,
                                 "INSERT INTO users VALUES (1, 'Bob', 21);",
                                 &output,
                                 &err));
    ASSERT_TRUE(contains_text(err.buf, "PRIMARY KEY 값이 이미 존재합니다"));

    err.buf[0] = '\0';
    ASSERT_TRUE(!run_sql_capture(dir,
                                 "INSERT INTO users VALUES (2, 'LongName', 21);",
                                 &output,
                                 &err));
    ASSERT_TRUE(contains_text(err.buf, "문자열 길이 제한을 초과했습니다"));

    remove_test_dir(dir);
    return true;
}

static bool test_multi_row_insert_and_select_top_round_trip(void) {
    char template[] = "/tmp/mini_sql_top_XXXXXX";
    char *dir = mkdtemp(template);
    char schema_path[512];
    ErrorContext err = {0};
    char *output = NULL;

    ASSERT_TRUE(dir != NULL);

    snprintf(schema_path, sizeof(schema_path), "%s/users.schema", dir);
    ASSERT_TRUE(write_text_file(schema_path, "id,name,age\n"));

    ASSERT_TRUE(run_sql_capture(dir,
                                "INSERT INTO users VALUES (1, 'Alice', 20), (2, 'Bob', 21), (3, 'Carol', 19);"
                                "SELECT TOP 2 name, age FROM users ORDER BY age DESC;",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "INSERT 3"));
    ASSERT_TRUE(contains_text(output, "Bob"));
    ASSERT_TRUE(contains_text(output, "Alice"));
    ASSERT_TRUE(!contains_text(output, "Carol"));
    ASSERT_TRUE(contains_text(output, "(2개 행)"));

    free(output);
    remove_test_dir(dir);
    return true;
}

static bool test_select_limit_round_trip(void) {
    char template[] = "/tmp/mini_sql_limit_XXXXXX";
    char *dir = mkdtemp(template);
    char schema_path[512];
    ErrorContext err = {0};
    char *output = NULL;

    ASSERT_TRUE(dir != NULL);

    snprintf(schema_path, sizeof(schema_path), "%s/users.schema", dir);
    ASSERT_TRUE(write_text_file(schema_path, "id,name,age\n"));

    ASSERT_TRUE(run_sql_capture(dir,
                                "INSERT INTO users VALUES (1, 'Alice', 20), (2, 'Bob', 21), (3, 'Carol', 19);"
                                "SELECT name FROM users ORDER BY age ASC LIMIT 1;",
                                &output,
                                &err));
    ASSERT_TRUE(contains_text(output, "INSERT 3"));
    ASSERT_TRUE(contains_text(output, "Carol"));
    ASSERT_TRUE(!contains_text(output, "Alice"));
    ASSERT_TRUE(!contains_text(output, "Bob"));
    ASSERT_TRUE(contains_text(output, "(1개 행)"));

    free(output);
    remove_test_dir(dir);
    return true;
}

static void run_test(bool (*test_fn)(void), const char *name) {
    if (test_fn()) {
        printf("[통과] %s\n", name);
    } else {
        printf("[실패] %s\n", name);
        g_failures += 1;
    }
}

int main(void) {
    run_test(test_tokenizer_handles_comments_and_strings, "토크나이저가 주석과 문자열을 처리한다");
    run_test(test_parser_supports_multi_statement_where_clause, "파서가 다중 문장과 WHERE 절을 처리한다");
    run_test(test_parser_supports_create_delete_and_drop, "파서가 CREATE DELETE DROP을 처리한다");
    run_test(test_parser_supports_schema_options, "파서가 PRIMARY KEY와 길이 옵션을 처리한다");
    run_test(test_parser_supports_insert_rows_and_select_limit, "파서가 TOP과 LIMIT의 충돌을 거부한다");
    run_test(test_parser_supports_top_and_order_by, "파서가 다중 VALUES와 TOP ORDER BY를 처리한다");
    run_test(test_insert_and_select_round_trip, "INSERT와 SELECT 왕복 실행");
    run_test(test_schema_qualified_table_and_csv_escape, "스키마 포함 테이블명과 CSV 이스케이프 처리");
    run_test(test_create_delete_and_drop_round_trip, "CREATE DELETE DROP 왕복 실행");
    run_test(test_primary_key_and_length_constraints, "PRIMARY KEY와 문자열 길이 제약을 검증한다");
    run_test(test_multi_row_insert_and_select_top_round_trip, "다중 INSERT와 TOP ORDER BY 왕복 실행");
    run_test(test_select_limit_round_trip, "LIMIT으로 일부 행만 조회한다");

    printf("총 %d개의 검증을 실행했습니다\n", g_tests);
    return g_failures == 0 ? 0 : 1;
}

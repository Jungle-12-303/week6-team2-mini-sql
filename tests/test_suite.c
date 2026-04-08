#define _XOPEN_SOURCE 700

#include "mini_sql.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0;
static int g_tests = 0;

#define ASSERT_TRUE(expr) do { \
    g_tests += 1; \
    if (!(expr)) { \
        fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

#define ASSERT_STREQ(actual, expected) do { \
    g_tests += 1; \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "Assertion failed at %s:%d\nexpected: %s\nactual:   %s\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return false; \
    } \
} while (0)

static bool contains_text(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

static bool write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return false;
    }

    fputs(contents, file);
    fclose(file);
    return true;
}

static char *read_stream_all(FILE *stream) {
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

static bool run_sql_capture(const char *db_path, const char *sql, char **out_output, char *error_buf, size_t error_size) {
    FILE *stream = tmpfile();
    MiniSqlAppConfig config;
    MiniSqlApp *app;
    bool ok;

    if (stream == NULL) {
        set_error(error_buf, error_size, "failed to create temporary output stream");
        return false;
    }

    config.db_path = db_path;
    config.output = stream;
    app = mini_sql_app_create(&config, error_buf, error_size);
    if (app == NULL) {
        fclose(stream);
        return false;
    }

    ok = mini_sql_app_run_sql(app, sql, error_buf, error_size);
    if (!ok) {
        mini_sql_app_destroy(app);
        fclose(stream);
        return false;
    }

    *out_output = read_stream_all(stream);
    mini_sql_app_destroy(app);
    fclose(stream);

    if (*out_output == NULL) {
        set_error(error_buf, error_size, "failed to read temporary output stream");
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
    char error_buf[MSQL_ERROR_SIZE];

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users (name, age) VALUES ('O''Reilly', 20); -- comment\nSELECT * FROM users;",
        &tokens,
        error_buf,
        sizeof(error_buf)
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
    char error_buf[MSQL_ERROR_SIZE];

    ASSERT_TRUE(tokenize_sql(
        "INSERT INTO users VALUES (1, 'Alice', 20); SELECT id, name FROM users WHERE age = 20;",
        &tokens,
        error_buf,
        sizeof(error_buf)
    ));
    ASSERT_TRUE(parse_tokens(&tokens, &statements, error_buf, sizeof(error_buf)));
    ASSERT_TRUE(statements.count == 2U);
    ASSERT_TRUE(statements.items[0].type == STATEMENT_INSERT);
    ASSERT_TRUE(statements.items[1].type == STATEMENT_SELECT);
    ASSERT_STREQ(statements.items[1].as.select_stmt.where_column, "age");
    ASSERT_STREQ(statements.items[1].as.select_stmt.where_value, "20");

    free_statement_list(&statements);
    free_token_list(&tokens);
    return true;
}

static bool test_insert_and_select_round_trip(void) {
    char template[] = "/tmp/mini_sql_test_XXXXXX";
    char *dir = mkdtemp(template);
    char schema_path[512];
    char data_path[512];
    char error_buf[MSQL_ERROR_SIZE];
    char *output = NULL;
    char *data_contents = NULL;

    ASSERT_TRUE(dir != NULL);

    snprintf(schema_path, sizeof(schema_path), "%s/users.schema", dir);
    ASSERT_TRUE(write_text_file(schema_path, "id,name,age\n"));

    ASSERT_TRUE(run_sql_capture(dir,
                                "INSERT INTO users VALUES (1, 'Alice', 20);"
                                "SELECT id, name FROM users WHERE age = 20;",
                                &output,
                                error_buf,
                                sizeof(error_buf)));
    ASSERT_TRUE(contains_text(output, "INSERT 1"));
    ASSERT_TRUE(contains_text(output, "Alice"));
    ASSERT_TRUE(contains_text(output, "(1 row)"));

    snprintf(data_path, sizeof(data_path), "%s/users.data", dir);
    ASSERT_TRUE(read_file_all(data_path, &data_contents, error_buf, sizeof(error_buf)));
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
    char error_buf[MSQL_ERROR_SIZE];
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
                                error_buf,
                                sizeof(error_buf)));
    ASSERT_TRUE(contains_text(output, "hello, \"sql\""));
    ASSERT_TRUE(contains_text(output, "(1 row)"));

    free(output);
    remove_test_dir(dir);
    return true;
}

static void run_test(bool (*test_fn)(void), const char *name) {
    if (test_fn()) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
        g_failures += 1;
    }
}

int main(void) {
    run_test(test_tokenizer_handles_comments_and_strings, "tokenizer handles comments and strings");
    run_test(test_parser_supports_multi_statement_where_clause, "parser supports multi statement where clause");
    run_test(test_insert_and_select_round_trip, "insert and select round trip");
    run_test(test_schema_qualified_table_and_csv_escape, "schema qualified table and csv escape");

    printf("Executed %d assertions\n", g_tests);
    return g_failures == 0 ? 0 : 1;
}

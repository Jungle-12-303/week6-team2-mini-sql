#include "common.h"
#include "executor.h"
#include "parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int tests_run = 0;
static int tests_failed = 0;

#define EXPECT_TRUE(condition, message)               \
    do {                                              \
        if (!(condition)) {                           \
            fprintf(stderr, "%s\n", (message));       \
            return 0;                                 \
        }                                             \
    } while (0)

#define RUN_TEST(test_function)                                      \
    do {                                                             \
        tests_run++;                                                 \
        if (test_function()) {                                       \
            printf("[PASS] %s\n", #test_function);                   \
        } else {                                                     \
            printf("[FAIL] %s\n", #test_function);                   \
            tests_failed++;                                          \
        }                                                            \
    } while (0)

static int ensure_directory(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return 1;
    }

    if (errno == EEXIST) {
        return 1;
    }

    return 0;
}

static int write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return 0;
    }

    fputs(contents, file);
    fclose(file);
    return 1;
}

static int read_stream(FILE *stream, char *buffer, size_t buffer_size) {
    size_t read_size;

    if (buffer_size == 0) {
        return 0;
    }

    rewind(stream);
    read_size = fread(buffer, 1, buffer_size - 1, stream);
    if (ferror(stream)) {
        return 0;
    }

    buffer[read_size] = '\0';
    return 1;
}

static int test_parse_insert_case_insensitive(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(parse_sql("insert into users values (1, 'Alice', 20);",
                          &stmt,
                          error,
                          sizeof(error)),
                "Expected lowercase INSERT to parse");
    EXPECT_TRUE(stmt.type == STATEMENT_INSERT, "Expected INSERT statement type");
    EXPECT_TRUE(strcmp(stmt.table_name, "users") == 0, "Expected table name users");
    EXPECT_TRUE(stmt.value_count == 3, "Expected 3 values");
    EXPECT_TRUE(strcmp(stmt.values[0], "1") == 0, "Expected first value to be 1");
    EXPECT_TRUE(strcmp(stmt.values[1], "Alice") == 0,
                "Expected second value to be Alice");
    EXPECT_TRUE(strcmp(stmt.values[2], "20") == 0, "Expected third value to be 20");

    return 1;
}

static int test_parse_select_specific_columns(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(parse_sql("SELECT name, id FROM users;", &stmt, error, sizeof(error)),
                error);
    EXPECT_TRUE(stmt.type == STATEMENT_SELECT, "Expected SELECT statement type");
    EXPECT_TRUE(stmt.select_all == 0, "Expected specific-column SELECT");
    EXPECT_TRUE(stmt.select_column_count == 2, "Expected 2 selected columns");
    EXPECT_TRUE(strcmp(stmt.select_columns[0], "name") == 0,
                "Expected first selected column to be name");
    EXPECT_TRUE(strcmp(stmt.select_columns[1], "id") == 0,
                "Expected second selected column to be id");
    EXPECT_TRUE(strcmp(stmt.table_name, "users") == 0, "Expected table name users");

    return 1;
}

static int test_insert_and_select_flow(void) {
    const char *data_dir = "test_output/flow";
    char table_path[MAX_PATH_LENGTH];
    Statement stmt;
    char error[MAX_ERROR_LENGTH];
    FILE *capture;
    char output[MAX_LINE_LENGTH * 4];

    EXPECT_TRUE(ensure_directory("test_output"), "Failed to create test_output");
    EXPECT_TRUE(ensure_directory(data_dir), "Failed to create flow directory");

    snprintf(table_path, sizeof(table_path), "%s/users.csv", data_dir);
    EXPECT_TRUE(write_text_file(table_path, "id,name,age\n"),
                "Failed to prepare users.csv");

    EXPECT_TRUE(parse_sql("INSERT INTO users VALUES (1, 'Alice', 20);",
                          &stmt,
                          error,
                          sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                error);
    fclose(capture);

    EXPECT_TRUE(parse_sql("SELECT * FROM users;", &stmt, error, sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                error);
    EXPECT_TRUE(read_stream(capture, output, sizeof(output)),
                "Failed to read SELECT output");
    fclose(capture);

    EXPECT_TRUE(strstr(output, "id,name,age") != NULL, "Missing CSV header");
    EXPECT_TRUE(strstr(output, "1,Alice,20") != NULL, "Missing inserted row");
    EXPECT_TRUE(strstr(output, "Rows: 1") != NULL, "Missing row count");

    return 1;
}

static int test_select_specific_columns_flow(void) {
    const char *data_dir = "test_output/select_columns";
    char table_path[MAX_PATH_LENGTH];
    Statement stmt;
    char error[MAX_ERROR_LENGTH];
    FILE *capture;
    char output[MAX_LINE_LENGTH * 4];

    EXPECT_TRUE(ensure_directory("test_output"), "Failed to create test_output");
    EXPECT_TRUE(ensure_directory(data_dir), "Failed to create select_columns dir");

    snprintf(table_path, sizeof(table_path), "%s/users.csv", data_dir);
    EXPECT_TRUE(write_text_file(table_path, "id,name,age\n1,Alice,20\n2,Bob,25\n"),
                "Failed to prepare users.csv");

    EXPECT_TRUE(parse_sql("SELECT name, id FROM users;", &stmt, error, sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                error);
    EXPECT_TRUE(read_stream(capture, output, sizeof(output)),
                "Failed to read projected SELECT output");
    fclose(capture);

    EXPECT_TRUE(strstr(output, "name,id") != NULL, "Missing projected header");
    EXPECT_TRUE(strstr(output, "Alice,1") != NULL, "Missing first projected row");
    EXPECT_TRUE(strstr(output, "Bob,2") != NULL, "Missing second projected row");
    EXPECT_TRUE(strstr(output, "Rows: 2") != NULL, "Missing row count");

    return 1;
}

static int test_invalid_insert_syntax(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(!parse_sql("INSERT users VALUES (1);", &stmt, error, sizeof(error)),
                "Expected invalid INSERT syntax to fail");
    EXPECT_TRUE(strstr(error, "INTO") != NULL, "Expected INTO error message");
    return 1;
}

static int test_invalid_select_syntax(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(!parse_sql("SELECT users;", &stmt, error, sizeof(error)),
                "Expected invalid SELECT syntax to fail");
    EXPECT_TRUE(strstr(error, "FROM") != NULL,
                "Expected SELECT syntax error to mention FROM");
    return 1;
}

static int test_missing_semicolon(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(!parse_sql("SELECT * FROM users", &stmt, error, sizeof(error)),
                "Expected missing semicolon to fail");
    EXPECT_TRUE(strstr(error, "must end with ';'") != NULL,
                "Expected semicolon error message");
    return 1;
}

static int test_missing_table_file(void) {
    const char *data_dir = "test_output/missing_table";
    Statement stmt;
    char error[MAX_ERROR_LENGTH];
    FILE *capture;

    EXPECT_TRUE(ensure_directory("test_output"), "Failed to create test_output");
    EXPECT_TRUE(ensure_directory(data_dir), "Failed to create missing_table dir");
    EXPECT_TRUE(parse_sql("SELECT * FROM users;", &stmt, error, sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(!execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                "Expected missing table execution to fail");
    EXPECT_TRUE(strstr(error, "Table file not found") != NULL,
                "Expected missing table error message");
    fclose(capture);
    return 1;
}

static int test_empty_sql_input(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    EXPECT_TRUE(!parse_sql("   \n\t ", &stmt, error, sizeof(error)),
                "Expected empty SQL input to fail");
    EXPECT_TRUE(strstr(error, "empty") != NULL, "Expected empty input message");
    return 1;
}

static int test_column_count_mismatch(void) {
    const char *data_dir = "test_output/mismatch";
    char table_path[MAX_PATH_LENGTH];
    Statement stmt;
    char error[MAX_ERROR_LENGTH];
    FILE *capture;

    EXPECT_TRUE(ensure_directory("test_output"), "Failed to create test_output");
    EXPECT_TRUE(ensure_directory(data_dir), "Failed to create mismatch dir");

    snprintf(table_path, sizeof(table_path), "%s/users.csv", data_dir);
    EXPECT_TRUE(write_text_file(table_path, "id,name,age\n"),
                "Failed to prepare mismatch users.csv");

    EXPECT_TRUE(parse_sql("INSERT INTO users VALUES (1, 'Alice');",
                          &stmt,
                          error,
                          sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(!execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                "Expected column mismatch to fail");
    EXPECT_TRUE(strstr(error, "Column count mismatch") != NULL,
                "Expected column mismatch message");
    fclose(capture);
    return 1;
}

static int test_duplicate_id_insert(void) {
    const char *data_dir = "test_output/duplicate_id";
    char table_path[MAX_PATH_LENGTH];
    Statement stmt;
    char error[MAX_ERROR_LENGTH];
    FILE *capture;

    EXPECT_TRUE(ensure_directory("test_output"), "Failed to create test_output");
    EXPECT_TRUE(ensure_directory(data_dir), "Failed to create duplicate_id dir");

    snprintf(table_path, sizeof(table_path), "%s/users.csv", data_dir);
    EXPECT_TRUE(write_text_file(table_path, "id,name,age\n1,Alice,20\n"),
                "Failed to prepare duplicate users.csv");

    EXPECT_TRUE(parse_sql("INSERT INTO users VALUES (1, 'Bob', 22);",
                          &stmt,
                          error,
                          sizeof(error)),
                error);

    capture = tmpfile();
    EXPECT_TRUE(capture != NULL, "Failed to open temporary capture file");
    EXPECT_TRUE(!execute_statement(&stmt, data_dir, capture, error, sizeof(error)),
                "Expected duplicate id insert to fail");
    EXPECT_TRUE(strstr(error, "Duplicate id value") != NULL,
                "Expected duplicate id error message");
    fclose(capture);

    return 1;
}

int main(void) {
    RUN_TEST(test_parse_insert_case_insensitive);
    RUN_TEST(test_parse_select_specific_columns);
    RUN_TEST(test_insert_and_select_flow);
    RUN_TEST(test_select_specific_columns_flow);
    RUN_TEST(test_invalid_insert_syntax);
    RUN_TEST(test_invalid_select_syntax);
    RUN_TEST(test_missing_semicolon);
    RUN_TEST(test_missing_table_file);
    RUN_TEST(test_empty_sql_input);
    RUN_TEST(test_column_count_mismatch);
    RUN_TEST(test_duplicate_id_insert);

    printf("Tests run: %d\n", tests_run);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed == 0 ? 0 : 1;
}

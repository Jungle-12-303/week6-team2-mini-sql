#include "mini_sql.h"
#include "query_result.h"
#include "storage_engine.h"
#include "table_file.h"

#include <stdlib.h>
#include <string.h>

static void cleanup_insert_state(TableSchema *schema, char **row_values, size_t row_value_count, bool *assigned) {
    free_string_array(row_values, row_value_count);
    free(assigned);
    free_table_schema(schema);
}

static void cleanup_select_state(TableSchema *schema, int *selected_indexes, char **headers,
                                 ResultTable *results) {
    free(selected_indexes);
    free(headers);
    free_result_table(results);
    free_table_schema(schema);
}

typedef struct SelectScanState {
    const SelectStatement *statement;
    const TableSchema *schema;
    const int *selected_indexes;
    size_t selected_count;
    ResultTable *results;
} SelectScanState;

static bool prepare_insert_row(const InsertStatement *statement, const TableSchema *schema, char ***out_row_values,
                               bool **out_assigned, char *error_buf, size_t error_size) {
    char **row_values = calloc(schema->column_count, sizeof(*row_values));
    bool *assigned = calloc(schema->column_count, sizeof(*assigned));
    size_t i;

    if (row_values == NULL || assigned == NULL) {
        free(row_values);
        free(assigned);
        set_error(error_buf, error_size, "out of memory while preparing INSERT");
        return false;
    }

    for (i = 0; i < schema->column_count; ++i) {
        row_values[i] = msql_strdup("");
        if (row_values[i] == NULL) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(error_buf, error_size, "out of memory while preparing INSERT");
            return false;
        }
    }

    if (statement->column_count == 0U) {
        if (statement->value_count != schema->column_count) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(error_buf, error_size, "INSERT value count does not match schema column count");
            return false;
        }

        for (i = 0; i < schema->column_count; ++i) {
            free(row_values[i]);
            row_values[i] = msql_strdup(statement->values[i]);
            if (row_values[i] == NULL) {
                free_string_array(row_values, schema->column_count);
                free(assigned);
                set_error(error_buf, error_size, "out of memory while preparing INSERT");
                return false;
            }
        }
    } else {
        for (i = 0; i < statement->column_count; ++i) {
            int index = find_column_index(schema->columns, schema->column_count, statement->columns[i]);

            if (index < 0) {
                free_string_array(row_values, schema->column_count);
                free(assigned);
                set_error(error_buf, error_size, "unknown column '%s' in INSERT", statement->columns[i]);
                return false;
            }
            if (assigned[index]) {
                free_string_array(row_values, schema->column_count);
                free(assigned);
                set_error(error_buf, error_size, "duplicate column '%s' in INSERT", statement->columns[i]);
                return false;
            }

            free(row_values[index]);
            row_values[index] = msql_strdup(statement->values[i]);
            if (row_values[index] == NULL) {
                free_string_array(row_values, schema->column_count);
                free(assigned);
                set_error(error_buf, error_size, "out of memory while preparing INSERT");
                return false;
            }

            assigned[index] = true;
        }
    }

    *out_row_values = row_values;
    *out_assigned = assigned;
    return true;
}

static bool collect_selected_row(char **fields, size_t field_count, void *user_data,
                                 char *error_buf, size_t error_size) {
    SelectScanState *scan_state = (SelectScanState *) user_data;
    char **selected_values;
    size_t i;

    if (field_count != scan_state->schema->column_count) {
        set_error(error_buf, error_size, "corrupted row in table '%s': expected %zu columns but got %zu",
                  scan_state->statement->table_name, scan_state->schema->column_count, field_count);
        return false;
    }

    if (scan_state->statement->has_where) {
        int where_index = find_column_index(scan_state->schema->columns, scan_state->schema->column_count,
                                            scan_state->statement->where_column);

        if (where_index < 0) {
            set_error(error_buf, error_size, "unknown column '%s' in WHERE clause", scan_state->statement->where_column);
            return false;
        }

        if (strcmp(fields[where_index], scan_state->statement->where_value) != 0) {
            return true;
        }
    }

    selected_values = calloc(scan_state->selected_count, sizeof(*selected_values));
    if (selected_values == NULL) {
        set_error(error_buf, error_size, "out of memory while building result set");
        return false;
    }

    for (i = 0; i < scan_state->selected_count; ++i) {
        selected_values[i] = msql_strdup(fields[scan_state->selected_indexes[i]]);
        if (selected_values[i] == NULL) {
            free_string_array(selected_values, i);
            free(selected_values);
            set_error(error_buf, error_size, "out of memory while building result set");
            return false;
        }
    }

    if (!append_result_row(scan_state->results, selected_values, scan_state->selected_count, error_buf, error_size)) {
        free_string_array(selected_values, scan_state->selected_count);
        free(selected_values);
        return false;
    }

    return true;
}

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context,
                              char *error_buf, size_t error_size) {
    TableSchema schema = {0};
    char **row_values = NULL;
    bool *assigned = NULL;
    FILE *output = context->output == NULL ? stdout : context->output;
    bool ok = false;

    /* 1. 실행기는 저장 엔진에서 현재 테이블 스키마를 읽어온다. */
    if (!storage_engine_load_schema(context->storage_engine, statement->table_name, &schema, error_buf, error_size)) {
        return false;
    }

    /* 2. SQL 의미를 해석해서 스키마 순서에 맞는 논리 행을 만든다. */
    if (!prepare_insert_row(statement, &schema, &row_values, &assigned, error_buf, error_size)) {
        goto cleanup;
    }

    /* 3. 실제 저장 포맷과 물리 저장은 StorageEngine이 담당한다. */
    if (!storage_engine_append_row(context->storage_engine, statement->table_name, row_values, schema.column_count,
                                   error_buf, error_size)) {
        goto cleanup;
    }

    /* 4. 저장이 끝나면 SQL 계층은 표준 결과 메시지만 출력한다. */
    fprintf(output, "INSERT 1\n");
    ok = true;

cleanup:
    cleanup_insert_state(&schema, row_values, schema.column_count, assigned);
    return ok;
}

bool execute_select_statement(const SelectStatement *statement, const ExecutionContext *context,
                              char *error_buf, size_t error_size) {
    TableSchema schema = {0};
    int *selected_indexes = NULL;
    size_t selected_count;
    char **headers = NULL;
    ResultTable results = {0};
    SelectScanState scan_state = {0};
    FILE *output = context->output == NULL ? stdout : context->output;
    size_t i;
    bool ok = false;

    /* 1. 실행기는 저장 엔진에서 현재 테이블 스키마를 읽어온다. */
    if (!storage_engine_load_schema(context->storage_engine, statement->table_name, &schema, error_buf, error_size)) {
        return false;
    }

    selected_count = statement->select_all ? schema.column_count : statement->column_count;
    selected_indexes = calloc(selected_count, sizeof(*selected_indexes));
    headers = calloc(selected_count, sizeof(*headers));
    if (selected_indexes == NULL || headers == NULL) {
        set_error(error_buf, error_size, "out of memory while preparing SELECT");
        goto cleanup;
    }

    /* 2. SELECT * 이면 전체 컬럼을 쓰고, 아니면 요청된 컬럼 이름을 인덱스로 변환한다. */
    if (statement->select_all) {
        for (i = 0; i < selected_count; ++i) {
            selected_indexes[i] = (int) i;
            headers[i] = schema.columns[i];
        }
    } else {
        for (i = 0; i < selected_count; ++i) {
            int index = find_column_index(schema.columns, schema.column_count, statement->columns[i]);

            if (index < 0) {
                set_error(error_buf, error_size, "unknown column '%s' in SELECT", statement->columns[i]);
                goto cleanup;
            }

            selected_indexes[i] = index;
            headers[i] = schema.columns[index];
        }
    }

    /* 3. 결과 수집 계획을 만들고, 실제 스캔은 StorageEngine에 위임한다. */
    scan_state.statement = statement;
    scan_state.schema = &schema;
    scan_state.selected_indexes = selected_indexes;
    scan_state.selected_count = selected_count;
    scan_state.results = &results;

    if (!storage_engine_scan_rows(context->storage_engine, statement->table_name, collect_selected_row, &scan_state,
                                  error_buf, error_size)) {
        goto cleanup;
    }

    /* 4. SELECT 계층은 결과 집합을 표 형태로 출력하는 역할만 가진다. */
    print_result(output, headers, selected_count, &results);
    ok = true;

cleanup:
    cleanup_select_state(&schema, selected_indexes, headers, &results);
    return ok;
}

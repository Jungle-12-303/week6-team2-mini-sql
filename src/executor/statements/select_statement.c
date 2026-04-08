#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "result/result_table.h"
#include "storage/storage_engine.h"

#include <stdlib.h>
#include <string.h>

typedef struct SelectScanState {
    const SelectStatement *statement;
    const CatalogSchema *schema;
    const int *selected_indexes;
    size_t selected_count;
    int where_index;
    ResultTable *results;
} SelectScanState;

static bool collect_selected_row(char **fields, size_t field_count, void *user_data,
                                 ErrorContext *err) {
    SelectScanState *scan_state = (SelectScanState *) user_data;
    char **selected_values;
    size_t i;

    if (!sql_executor_validate_row_field_count(scan_state->schema, scan_state->statement->table_name,
                                               field_count, err)) {
        return false;
    }

    if (scan_state->where_index >= 0 &&
        strcmp(fields[scan_state->where_index], scan_state->statement->where.where_value) != 0) {
        return true;
    }

    selected_values = calloc(scan_state->selected_count, sizeof(*selected_values));
    if (selected_values == NULL) {
        set_error(err, "조회 결과를 만드는 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < scan_state->selected_count; ++i) {
        selected_values[i] = msql_strdup(fields[scan_state->selected_indexes[i]]);
        if (selected_values[i] == NULL) {
            set_error(err, "조회 결과를 만드는 중 메모리가 부족합니다");
            free_string_array(selected_values, i);
            free(selected_values);
            return false;
        }
    }

    if (!append_result_row(scan_state->results, selected_values, scan_state->selected_count, err)) {
        free_string_array(selected_values, scan_state->selected_count);
        free(selected_values);
        return false;
    }

    return true;
}

static bool prepare_select_projection(const SelectStatement *statement, const CatalogSchema *schema,
                                      int **out_selected_indexes, char ***out_headers,
                                      size_t *out_selected_count, ErrorContext *err) {
    size_t selected_count = statement->select_all ? schema->column_count : statement->column_count;
    int *selected_indexes;
    char **headers;
    size_t i;

    selected_indexes = calloc(selected_count, sizeof(*selected_indexes));
    headers = calloc(selected_count, sizeof(*headers));
    if (selected_indexes == NULL || headers == NULL) {
        set_error(err, "SELECT 실행 준비 중 메모리가 부족합니다");
        free(selected_indexes);
        free(headers);
        return false;
    }

    for (i = 0; i < selected_count; ++i) {
        int index = statement->select_all
                        ? (int) i
                        : find_column_index(schema->columns, schema->column_count, statement->columns[i]);

        if (index < 0) {
            set_error(err, "SELECT에 없는 컬럼이 사용되었습니다: %s", statement->columns[i]);
            free(selected_indexes);
            free(headers);
            return false;
        }

        selected_indexes[i] = index;
        headers[i] = schema->columns[index];
    }

    *out_selected_indexes = selected_indexes;
    *out_headers = headers;
    *out_selected_count = selected_count;
    return true;
}

bool execute_select_statement(const SelectStatement *statement, const ExecutionContext *context,
                              ErrorContext *err) {
    CatalogSchema schema = {0};
    int *selected_indexes = NULL;
    size_t selected_count;
    char **headers = NULL;
    ResultTable results = {0};
    SelectScanState scan_state = {0};
    bool ok = false;

    if (!sql_executor_load_schema(context, statement->table_name, &schema, err)) {
        return false;
    }

    if (!prepare_select_projection(statement, &schema, &selected_indexes, &headers, &selected_count, err)) {
        goto cleanup;
    }

    scan_state.statement = statement;
    scan_state.schema = &schema;
    scan_state.selected_indexes = selected_indexes;
    scan_state.selected_count = selected_count;
    scan_state.results = &results;
    scan_state.where_index = sql_executor_resolve_where_index(&schema, &statement->where, err);
    if (statement->where.has_where && scan_state.where_index < 0) {
        goto cleanup;
    }

    if (!storage_engine_scan_rows(context->storage_engine, statement->table_name,
                                  collect_selected_row, &scan_state, err)) {
        goto cleanup;
    }

    result_formatter_print(context->formatter, sql_executor_output(context),
                           headers, selected_count, &results);
    ok = true;

cleanup:
    free(selected_indexes);
    free(headers);
    free_result_table(&results);
    catalog_free_schema(&schema);
    return ok;
}

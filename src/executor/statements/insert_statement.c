#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

#include <stdlib.h>

static bool prepare_insert_row(const InsertStatement *statement, const CatalogSchema *schema,
                               char ***out_row_values, ErrorContext *err) {
    char **row_values = calloc(schema->column_count, sizeof(*row_values));
    bool *assigned = NULL;
    size_t input_count;
    size_t i;

    if (row_values == NULL) {
        set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < schema->column_count; ++i) {
        row_values[i] = msql_strdup("");
        if (row_values[i] == NULL) {
            free_string_array(row_values, schema->column_count);
            set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
            return false;
        }
    }

    input_count = statement->column_count == 0U ? schema->column_count : statement->column_count;
    if (statement->value_count != input_count) {
        free_string_array(row_values, schema->column_count);
        set_error(err, "INSERT 값 개수가 스키마 컬럼 개수와 다릅니다");
        return false;
    }

    assigned = calloc(schema->column_count, sizeof(*assigned));
    if (assigned == NULL) {
        free_string_array(row_values, schema->column_count);
        set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < input_count; ++i) {
        const char *column_name = statement->column_count == 0U ? schema->columns[i] : statement->columns[i];
        int index = statement->column_count == 0U ? (int) i
                                                  : find_column_index(schema->columns, schema->column_count, column_name);

        if (index < 0) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT에 없는 컬럼이 사용되었습니다: %s", column_name);
            return false;
        }
        if (assigned[index]) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT에 같은 컬럼이 중복으로 들어왔습니다: %s", column_name);
            return false;
        }

        free(row_values[index]);
        row_values[index] = msql_strdup(statement->values[i]);
        if (row_values[index] == NULL) {
            free_string_array(row_values, schema->column_count);
            free(assigned);
            set_error(err, "INSERT 실행 준비 중 메모리가 부족합니다");
            return false;
        }

        assigned[index] = true;
    }

    free(assigned);
    *out_row_values = row_values;
    return true;
}

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context,
                              ErrorContext *err) {
    CatalogSchema schema = {0};
    char **row_values = NULL;
    bool ok = false;

    if (!sql_executor_load_schema(context, statement->table_name, &schema, err)) {
        return false;
    }

    if (!prepare_insert_row(statement, &schema, &row_values, err)) {
        goto cleanup;
    }

    if (!storage_engine_append_row(context->storage_engine, statement->table_name,
                                   row_values, schema.column_count, err)) {
        goto cleanup;
    }

    sql_executor_print_status(context, "INSERT 1");
    ok = true;

cleanup:
    free_string_array(row_values, schema.column_count);
    catalog_free_schema(&schema);
    return ok;
}

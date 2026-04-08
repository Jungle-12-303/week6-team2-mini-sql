#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

#include <stdlib.h>

static bool build_catalog_schema_from_create_statement(const CreateTableStatement *statement,
                                                       CatalogSchema *schema, ErrorContext *err) {
    size_t i;
    int primary_key_index = -1;

    schema->columns = calloc(statement->column_count, sizeof(*schema->columns));
    schema->types = calloc(statement->column_count, sizeof(*schema->types));
    schema->max_lengths = calloc(statement->column_count, sizeof(*schema->max_lengths));
    schema->is_primary_keys = calloc(statement->column_count, sizeof(*schema->is_primary_keys));
    schema->primary_key_index = -1;
    schema->column_count = statement->column_count;
    if (schema->columns == NULL || schema->types == NULL ||
        schema->max_lengths == NULL || schema->is_primary_keys == NULL) {
        free(schema->columns);
        free(schema->types);
        free(schema->max_lengths);
        free(schema->is_primary_keys);
        schema->columns = NULL;
        schema->types = NULL;
        schema->max_lengths = NULL;
        schema->is_primary_keys = NULL;
        schema->column_count = 0U;
        set_error(err, "CREATE TABLE 실행 준비 중 메모리가 부족합니다");
        return false;
    }

    for (i = 0; i < statement->column_count; ++i) {
        if (find_column_index(schema->columns, i, statement->columns[i]) >= 0) {
            catalog_free_schema(schema);
            set_error(err, "CREATE TABLE에 같은 컬럼이 중복으로 들어왔습니다: %s", statement->columns[i]);
            return false;
        }

        schema->columns[i] = msql_strdup(statement->columns[i]);
        schema->types[i] = msql_strdup(statement->column_types[i]);
        if (schema->columns[i] == NULL || schema->types[i] == NULL) {
            catalog_free_schema(schema);
            set_error(err, "CREATE TABLE 실행 준비 중 메모리가 부족합니다");
            return false;
        }
        schema->max_lengths[i] = statement->column_sizes[i];
        schema->is_primary_keys[i] = statement->column_is_primary_keys[i];
        if (statement->column_is_primary_keys[i]) {
            if (primary_key_index >= 0) {
                catalog_free_schema(schema);
                set_error(err, "현재는 PRIMARY KEY 컬럼을 하나만 지원합니다");
                return false;
            }
            primary_key_index = (int) i;
        }
        if (statement->column_sizes[i] > 0U &&
            !(strings_equal_ci(statement->column_types[i], "TEXT") ||
              strings_equal_ci(statement->column_types[i], "VARCHAR") ||
              strings_equal_ci(statement->column_types[i], "CHAR"))) {
            catalog_free_schema(schema);
            set_error(err, "길이 제한은 TEXT, VARCHAR, CHAR 타입에서만 사용할 수 있습니다: %s",
                      statement->columns[i]);
            return false;
        }
    }

    schema->primary_key_index = primary_key_index;

    return true;
}

bool execute_create_table_statement(const CreateTableStatement *statement, const ExecutionContext *context,
                                    ErrorContext *err) {
    CatalogSchema schema = {0};

    if (!build_catalog_schema_from_create_statement(statement, &schema, err)) {
        return false;
    }

    if (!storage_engine_create_table(context->storage_engine, statement->table_name, &schema, err)) {
        catalog_free_schema(&schema);
        return false;
    }

    sql_executor_print_status(context, "CREATE TABLE");
    catalog_free_schema(&schema);
    return true;
}

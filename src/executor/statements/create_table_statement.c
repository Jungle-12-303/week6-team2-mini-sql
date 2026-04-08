#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

#include <stdlib.h>

static bool build_catalog_schema_from_create_statement(const CreateTableStatement *statement,
                                                       CatalogSchema *schema, ErrorContext *err) {
    size_t i;

    schema->columns = calloc(statement->column_count, sizeof(*schema->columns));
    schema->types = calloc(statement->column_count, sizeof(*schema->types));
    schema->column_count = statement->column_count;
    if (schema->columns == NULL || schema->types == NULL) {
        free(schema->columns);
        free(schema->types);
        schema->columns = NULL;
        schema->types = NULL;
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
    }

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

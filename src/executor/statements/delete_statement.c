#include "executor/statements/sql_statement_handlers.h"

#include "catalog/schema_catalog.h"
#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

#include <string.h>

typedef struct DeleteScanState {
    const DeleteStatement *statement;
    const CatalogSchema *schema;
    int where_index;
} DeleteScanState;

static bool should_delete_row(char **fields, size_t field_count, void *user_data, bool *out_match,
                              ErrorContext *err) {
    DeleteScanState *delete_state = (DeleteScanState *) user_data;

    if (!sql_executor_validate_row_field_count(delete_state->schema, delete_state->statement->table_name,
                                               field_count, err)) {
        return false;
    }

    if (delete_state->where_index < 0) {
        *out_match = true;
        return true;
    }

    *out_match = strcmp(fields[delete_state->where_index], delete_state->statement->where.where_value) == 0;
    return true;
}

bool execute_delete_statement(const DeleteStatement *statement, const ExecutionContext *context,
                              ErrorContext *err) {
    CatalogSchema schema = {0};
    DeleteScanState delete_state = {0};
    size_t deleted_count = 0U;
    bool ok = false;

    if (!sql_executor_load_schema(context, statement->table_name, &schema, err)) {
        return false;
    }

    delete_state.statement = statement;
    delete_state.schema = &schema;
    delete_state.where_index = sql_executor_resolve_where_index(&schema, &statement->where, err);
    if (statement->where.has_where && delete_state.where_index < 0) {
        goto cleanup;
    }

    if (!storage_engine_delete_rows(context->storage_engine, statement->table_name,
                                    should_delete_row, &delete_state, &deleted_count, err)) {
        goto cleanup;
    }

    fprintf(sql_executor_output(context), "DELETE %zu\n", deleted_count);
    ok = true;

cleanup:
    catalog_free_schema(&schema);
    return ok;
}

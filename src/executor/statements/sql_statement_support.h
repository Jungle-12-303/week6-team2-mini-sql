#ifndef SQL_STATEMENT_SUPPORT_H
#define SQL_STATEMENT_SUPPORT_H

#include "catalog/schema_catalog.h"
#include "mini_sql.h"

FILE *sql_executor_output(const ExecutionContext *context);
void sql_executor_print_status(const ExecutionContext *context, const char *message);
bool sql_executor_load_schema(const ExecutionContext *context, const char *table_name,
                              CatalogSchema *schema, ErrorContext *err);
bool sql_executor_validate_row_field_count(const CatalogSchema *schema, const char *table_name,
                                           size_t field_count, ErrorContext *err);
int sql_executor_resolve_where_index(const CatalogSchema *schema, const WhereClause *where,
                                     ErrorContext *err);

#endif

#include <stdio.h>
#include "executor.h"
#include "storage.h"
#include "display.h"

/* This runs INSERT because the CLI should not know storage details. */
static int execute_insert(const Query *query) {
    if (!append_user(query)) {
        printf("insert failed\n");
        return 0;
    }
    return 1;
}

/* This runs SELECT because the executor owns query-type branching. */
static int execute_select(const Query *query) {
    TableSchema schema;
    TableData data;

    if (!load_schema(query->table_name, &schema)) {
        printf("table not found\n");
        return 0;
    }
    if (!load_rows(query->table_name, &data)) {
        printf("select failed\n");
        return 0;
    }
    return print_select_result(query, &schema, &data);
}

/* This dispatches by QueryType so main stays small when features grow. */
int execute_query(const Query *query) {
    if (query->type == QUERY_INSERT) {
        return execute_insert(query);
    }
    if (query->type == QUERY_SELECT) {
        return execute_select(query);
    }
    return 0;
}

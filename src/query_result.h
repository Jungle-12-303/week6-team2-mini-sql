#ifndef QUERY_RESULT_H
#define QUERY_RESULT_H

#include "mini_sql.h"

typedef struct ResultRow {
    char **values;
    size_t count;
} ResultRow;

typedef struct ResultTable {
    ResultRow *rows;
    size_t row_count;
    size_t capacity;
} ResultTable;

bool append_result_row(ResultTable *table, char **values, size_t count, char *error_buf, size_t error_size);
void free_result_table(ResultTable *table);
void print_result(FILE *output, char **headers, size_t column_count, const ResultTable *rows);

#endif

#ifndef RESULT_TABLE_H
#define RESULT_TABLE_H

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

struct ResultFormatter {
    void (*print)(FILE *output, char **headers, size_t column_count, const ResultTable *rows);
};

bool append_result_row(ResultTable *table, char **values, size_t count, ErrorContext *err);
void free_result_table(ResultTable *table);
void result_formatter_print(const ResultFormatter *formatter, FILE *output,
                            char **headers, size_t column_count, const ResultTable *rows);

#endif

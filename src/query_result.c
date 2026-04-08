#include "query_result.h"

#include <stdlib.h>
#include <string.h>

bool append_result_row(ResultTable *table, char **values, size_t count, char *error_buf, size_t error_size) {
    ResultRow *new_rows;
    size_t new_capacity;

    if (table->row_count >= table->capacity) {
        new_capacity = table->capacity == 0U ? 8U : table->capacity * 2U;
        new_rows = realloc(table->rows, new_capacity * sizeof(*new_rows));
        if (new_rows == NULL) {
            set_error(error_buf, error_size, "out of memory while building result set");
            return false;
        }
        table->rows = new_rows;
        table->capacity = new_capacity;
    }

    table->rows[table->row_count].values = values;
    table->rows[table->row_count].count = count;
    table->row_count += 1U;
    return true;
}

void free_result_table(ResultTable *table) {
    size_t i;

    for (i = 0; i < table->row_count; ++i) {
        free_string_array(table->rows[i].values, table->rows[i].count);
    }
    free(table->rows);
    table->rows = NULL;
    table->row_count = 0U;
    table->capacity = 0U;
}

static void print_separator(FILE *output, const size_t *widths, size_t column_count) {
    size_t i;
    size_t j;

    for (i = 0; i < column_count; ++i) {
        fputc('+', output);
        for (j = 0U; j < widths[i] + 2U; ++j) {
            fputc('-', output);
        }
    }
    fputs("+\n", output);
}

static void print_row(FILE *output, char **values, const size_t *widths, size_t column_count) {
    size_t i;

    for (i = 0; i < column_count; ++i) {
        fprintf(output, "| %-*s ", (int) widths[i], values[i]);
    }
    fputs("|\n", output);
}

void print_result(FILE *output, char **headers, size_t column_count, const ResultTable *rows) {
    size_t *widths = calloc(column_count, sizeof(*widths));
    size_t i;
    size_t j;

    if (widths == NULL) {
        return;
    }

    for (i = 0; i < column_count; ++i) {
        widths[i] = strlen(headers[i]);
    }

    for (i = 0; i < rows->row_count; ++i) {
        for (j = 0; j < column_count; ++j) {
            size_t length = strlen(rows->rows[i].values[j]);
            if (length > widths[j]) {
                widths[j] = length;
            }
        }
    }

    print_separator(output, widths, column_count);
    print_row(output, headers, widths, column_count);
    print_separator(output, widths, column_count);
    for (i = 0; i < rows->row_count; ++i) {
        print_row(output, rows->rows[i].values, widths, column_count);
    }
    print_separator(output, widths, column_count);
    fprintf(output, "(%zu %s)\n", rows->row_count, rows->row_count == 1U ? "row" : "rows");

    free(widths);
}

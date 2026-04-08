#include <stdio.h>
#include <string.h>
#include "display.h"

/* This finds a schema column index so SELECT can map names to stored values. */
static int find_column_index(const TableSchema *schema, const char *name) {
    int i;

    for (i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* This chooses which columns the current SELECT should print. */
static int build_selected_indices(const Query *query, const TableSchema *schema, int indices[3], int *count) {
    int i;

    if (query->select_all) {
        *count = schema->column_count;
        for (i = 0; i < *count; i++) {
            indices[i] = i;
        }
        return 1;
    }

    *count = query->selected_column_count;
    for (i = 0; i < *count; i++) {
        indices[i] = find_column_index(schema, query->selected_columns[i]);
        if (indices[i] < 0) {
            return 0;
        }
    }
    return 1;
}

/* This prints a border so the result looks like a small SQL table. */
static void print_border(const int widths[3], int count) {
    int i;
    int j;

    for (i = 0; i < count; i++) {
        putchar('+');
        for (j = 0; j < widths[i] + 2; j++) {
            putchar('-');
        }
    }
    puts("+");
}

/* This prints one table row using the prepared column widths. */
static void print_row(char values[3][32], const int widths[3], int count) {
    int i;

    for (i = 0; i < count; i++) {
        printf("| %-*s ", widths[i], values[i]);
    }
    puts("|");
}

/* This prints SELECT output because executor should not know display details. */
int print_select_result(const Query *query, const TableSchema *schema, const TableData *data) {
    int indices[3];
    int widths[3] = {0, 0, 0};
    char cells[3][32];
    int count;
    int row;
    int col;
    int len;

    /* 1. Decide which columns to show */
    if (!build_selected_indices(query, schema, indices, &count)) {
        printf("invalid column\n");
        return 0;
    }

    /* 2. Compute simple column widths */
    for (col = 0; col < count; col++) {
        widths[col] = (int)strlen(schema->names[indices[col]]);
    }
    for (row = 0; row < data->row_count; row++) {
        for (col = 0; col < count; col++) {
            len = (int)strlen(data->values[row][indices[col]]);
            if (len > widths[col]) {
                widths[col] = len;
            }
        }
    }

    /* 3. Print header and rows */
    print_border(widths, count);
    for (col = 0; col < count; col++) {
        strcpy(cells[col], schema->names[indices[col]]);
    }
    print_row(cells, widths, count);
    print_border(widths, count);
    for (row = 0; row < data->row_count; row++) {
        for (col = 0; col < count; col++) {
            strcpy(cells[col], data->values[row][indices[col]]);
        }
        print_row(cells, widths, count);
    }
    print_border(widths, count);
    return 1;
}

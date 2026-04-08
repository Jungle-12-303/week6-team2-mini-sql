#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../04_common/common.h"
#include "../06_storage/storage.h"

static void make_table_path(const char *table_name, char *path, int path_size) {
    snprintf(path, path_size, "03_data/%s.csv", table_name);
}

static void trim_newline(char *text) {
    size_t length;

    length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[length - 1] = '\0';
        length--;
    }
}

static int split_csv_line(char *line, char cells[][MAX_CELL_LENGTH]) {
    int count;
    char *token;

    count = 0;
    token = strtok(line, ",");
    while (token != NULL && count < MAX_VALUES) {
        snprintf(cells[count], MAX_CELL_LENGTH, "%s", token);
        count++;
        token = strtok(NULL, ",");
    }

    return count;
}

static int get_display_width(const char *text) {
    mbstate_t state;
    wchar_t wide_char;
    size_t consumed;
    int total_width;
    int char_width;

    memset(&state, 0, sizeof(state));
    total_width = 0;

    while (*text != '\0') {
        consumed = mbrtowc(&wide_char, text, MB_CUR_MAX, &state);
        if (consumed == (size_t)-1 || consumed == (size_t)-2) {
            memset(&state, 0, sizeof(state));
            total_width++;
            text++;
            continue;
        }

        if (consumed == 0) {
            break;
        }

        char_width = wcwidth(wide_char);
        if (char_width < 0) {
            char_width = 1;
        }

        total_width += char_width;
        text += consumed;
    }

    return total_width;
}

static void build_default_headers(const char *table_name, int column_count,
                                  char headers[][MAX_CELL_LENGTH]) {
    static const char *materials_four_columns[] = {
        "product_name",
        "material_name",
        "color_name",
        "product_weight"
    };
    int index;

    if (strcmp(table_name, "materials") == 0 && column_count == 4) {
        for (index = 0; index < column_count; index++) {
            snprintf(headers[index], MAX_CELL_LENGTH, "%s", materials_four_columns[index]);
        }
        return;
    }

    for (index = 0; index < column_count; index++) {
        snprintf(headers[index], MAX_CELL_LENGTH, "col%d", index + 1);
    }
}

static void print_separator(const int column_widths[], int column_count) {
    int index;
    int space;

    printf("+");
    for (index = 0; index < column_count; index++) {
        for (space = 0; space < column_widths[index] + 2; space++) {
            printf("-");
        }
        printf("+");
    }
    printf("\n");
}

static void print_row(char cells[][MAX_CELL_LENGTH], const int column_widths[], int column_count) {
    int index;
    int padding;

    printf("|");
    for (index = 0; index < column_count; index++) {
        padding = column_widths[index] - get_display_width(cells[index]);
        if (padding < 0) {
            padding = 0;
        }

        printf(" %s", cells[index]);
        while (padding > 0) {
            printf(" ");
            padding--;
        }
        printf(" |");
    }
    printf("\n");
}

int append_row_to_table(const char *table_name, const char values[][128], int value_count,
                        char *error_message, int error_size) {
    char path[256];
    FILE *file;
    int index;

    if (strcmp(table_name, "materials") != 0) {
        snprintf(error_message, error_size, "only materials table is supported");
        return 0;
    }

    make_table_path(table_name, path, sizeof(path));

    file = fopen(path, "a");
    if (file == NULL) {
        snprintf(error_message, error_size, "cannot open table file for append: %s", path);
        return 0;
    }

    for (index = 0; index < value_count; index++) {
        fprintf(file, "%s", values[index]);
        if (index < value_count - 1) {
            fprintf(file, ",");
        }
    }
    fprintf(file, "\n");

    fclose(file);
    return 1;
}

int print_table_rows(const char *table_name, char *error_message, int error_size) {
    char path[256];
    char line[1024];
    FILE *file;
    char rows[MAX_ROWS][MAX_VALUES][MAX_CELL_LENGTH];
    char headers[MAX_VALUES][MAX_CELL_LENGTH];
    int column_widths[MAX_VALUES];
    int row_count;
    int column_count;
    int row_index;
    int column_index;

    if (strcmp(table_name, "materials") != 0) {
        snprintf(error_message, error_size, "only materials table is supported");
        return 0;
    }

    make_table_path(table_name, path, sizeof(path));

    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error_message, error_size, "table file does not exist: %s", path);
        return 0;
    }

    row_count = 0;
    column_count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        if (row_count >= MAX_ROWS) {
            fclose(file);
            snprintf(error_message, error_size, "too many rows in table: %s", table_name);
            return 0;
        }

        column_count = split_csv_line(line, rows[row_count]);
        if (column_count <= 0) {
            continue;
        }

        row_count++;
    }

    fclose(file);

    if (row_count == 0 || column_count == 0) {
        printf("+--+\n");
        printf("|  |\n");
        printf("+--+\n");
        return 1;
    }

    build_default_headers(table_name, column_count, headers);

    for (column_index = 0; column_index < column_count; column_index++) {
        column_widths[column_index] = get_display_width(headers[column_index]);
    }

    for (row_index = 0; row_index < row_count; row_index++) {
        for (column_index = 0; column_index < column_count; column_index++) {
            int cell_length;

            cell_length = get_display_width(rows[row_index][column_index]);
            if (cell_length > column_widths[column_index]) {
                column_widths[column_index] = cell_length;
            }
        }
    }

    print_separator(column_widths, column_count);
    print_row(headers, column_widths, column_count);
    print_separator(column_widths, column_count);
    for (row_index = 0; row_index < row_count; row_index++) {
        print_row(rows[row_index], column_widths, column_count);
    }
    print_separator(column_widths, column_count);
    return 1;
}

#include "storage.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static int build_table_path(const char *data_dir,
                            const char *table_name,
                            char *path,
                            size_t path_size,
                            char *error,
                            size_t error_size) {
    int written = snprintf(path, path_size, "%s/%s.csv", data_dir, table_name);

    if (written < 0 || (size_t)written >= path_size) {
        set_error(error, error_size, "Table path is too long");
        return 0;
    }

    return 1;
}

static void strip_line_end(char *line) {
    size_t length = strlen(line);

    while (length > 0 &&
           (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

static void trim_in_place(char *text) {
    char *start = text;
    char *end;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    *end = '\0';
}

static int line_is_blank(const char *line) {
    while (*line != '\0') {
        if (!isspace((unsigned char)*line)) {
            return 0;
        }

        line++;
    }

    return 1;
}

static int strings_equal_ignore_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }

        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static int split_csv_line(const char *line,
                          char fields[][MAX_VALUE_LENGTH],
                          int max_fields,
                          char *error,
                          size_t error_size) {
    char copy[MAX_LINE_LENGTH];
    size_t length = strlen(line);
    size_t start = 0;
    size_t index;
    int field_count = 0;

    if (length >= sizeof(copy)) {
        set_error(error, error_size, "CSV line is too long");
        return -1;
    }

    memcpy(copy, line, length + 1);
    strip_line_end(copy);

    if (copy[0] == '\0') {
        return 0;
    }

    for (index = 0;; index++) {
        if (copy[index] == ',' || copy[index] == '\0') {
            size_t field_length = index - start;

            if (field_count >= max_fields) {
                set_error(error, error_size, "Too many CSV columns");
                return -1;
            }

            if (field_length >= MAX_VALUE_LENGTH) {
                set_error(error, error_size, "CSV field is too long");
                return -1;
            }

            memcpy(fields[field_count], copy + start, field_length);
            fields[field_count][field_length] = '\0';
            trim_in_place(fields[field_count]);
            field_count++;

            if (copy[index] == '\0') {
                break;
            }

            start = index + 1;
        }
    }

    return field_count;
}

static int find_column_index(char fields[][MAX_VALUE_LENGTH],
                             int field_count,
                             const char *name) {
    int index;

    for (index = 0; index < field_count; index++) {
        if (strings_equal_ignore_case(fields[index], name)) {
            return index;
        }
    }

    return -1;
}

static void write_selected_fields(FILE *out,
                                  char fields[][MAX_VALUE_LENGTH],
                                  const int *selected_indexes,
                                  int selected_count) {
    int index;

    for (index = 0; index < selected_count; index++) {
        if (index > 0) {
            fputc(',', out);
        }

        fputs(fields[selected_indexes[index]], out);
    }

    fputc('\n', out);
}

int storage_append_row(const char *data_dir,
                       const Statement *stmt,
                       char *error,
                       size_t error_size) {
    char path[MAX_PATH_LENGTH];
    char header[MAX_LINE_LENGTH];
    char header_fields[MAX_VALUES][MAX_VALUE_LENGTH];
    char row_fields[MAX_VALUES][MAX_VALUE_LENGTH];
    char line[MAX_LINE_LENGTH];
    FILE *file;
    int header_columns;
    int id_column_index;
    int index;
    int row_columns;

    if (!build_table_path(data_dir,
                          stmt->table_name,
                          path,
                          sizeof(path),
                          error,
                          error_size)) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "Table file not found: %s", path);
        return 0;
    }

    if (fgets(header, sizeof(header), file) == NULL) {
        fclose(file);
        snprintf(error, error_size, "Table file is empty: %s", path);
        return 0;
    }

    header_columns = split_csv_line(header,
                                    header_fields,
                                    MAX_VALUES,
                                    error,
                                    error_size);
    if (header_columns <= 0) {
        fclose(file);
        snprintf(error, error_size, "Invalid table header: %s", path);
        return 0;
    }

    if (header_columns != stmt->value_count) {
        snprintf(error,
                 error_size,
                 "Column count mismatch: expected %d values, got %d",
                 header_columns,
                 stmt->value_count);
        fclose(file);
        return 0;
    }

    id_column_index = find_column_index(header_fields, header_columns, "id");

    if (id_column_index >= 0) {
        while (fgets(line, sizeof(line), file) != NULL) {
            if (line_is_blank(line)) {
                continue;
            }

            row_columns = split_csv_line(line,
                                         row_fields,
                                         header_columns,
                                         error,
                                         error_size);
            if (row_columns < 0) {
                fclose(file);
                return 0;
            }

            if (row_columns != header_columns) {
                fclose(file);
                snprintf(error, error_size, "Invalid row in table file: %s", path);
                return 0;
            }

            if (strcmp(row_fields[id_column_index],
                       stmt->values[id_column_index]) == 0) {
                fclose(file);
                snprintf(error,
                         error_size,
                         "Duplicate id value: %s",
                         stmt->values[id_column_index]);
                return 0;
            }
        }
    }

    fclose(file);
    file = fopen(path, "a");
    if (file == NULL) {
        snprintf(error, error_size, "Cannot open table file for append: %s", path);
        return 0;
    }

    for (index = 0; index < stmt->value_count; index++) {
        if (index > 0) {
            fputc(',', file);
        }

        fputs(stmt->values[index], file);
    }

    fputc('\n', file);
    fclose(file);
    return 1;
}

int storage_select_all(const char *data_dir,
                       const Statement *stmt,
                       FILE *out,
                       char *error,
                       size_t error_size) {
    char path[MAX_PATH_LENGTH];
    char header_fields[MAX_VALUES][MAX_VALUE_LENGTH];
    char row_fields[MAX_VALUES][MAX_VALUE_LENGTH];
    char line[MAX_LINE_LENGTH];
    FILE *file;
    int header_columns;
    int selected_indexes[MAX_VALUES];
    int selected_count;
    int row_columns;
    int index;
    int row_count = 0;

    if (!build_table_path(data_dir,
                          stmt->table_name,
                          path,
                          sizeof(path),
                          error,
                          error_size)) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "Table file not found: %s", path);
        return 0;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        snprintf(error, error_size, "Table file is empty: %s", path);
        return 0;
    }

    header_columns = split_csv_line(line,
                                    header_fields,
                                    MAX_VALUES,
                                    error,
                                    error_size);
    if (header_columns <= 0) {
        fclose(file);
        snprintf(error, error_size, "Invalid table header: %s", path);
        return 0;
    }

    if (stmt->select_all) {
        selected_count = header_columns;
        for (index = 0; index < header_columns; index++) {
            selected_indexes[index] = index;
        }
    } else {
        selected_count = stmt->select_column_count;
        for (index = 0; index < selected_count; index++) {
            selected_indexes[index] = find_column_index(header_fields,
                                                        header_columns,
                                                        stmt->select_columns[index]);
            if (selected_indexes[index] < 0) {
                fclose(file);
                snprintf(error,
                         error_size,
                         "Unknown column in SELECT: %s",
                         stmt->select_columns[index]);
                return 0;
            }
        }
    }

    write_selected_fields(out, header_fields, selected_indexes, selected_count);

    while (fgets(line, sizeof(line), file) != NULL) {
        if (line_is_blank(line)) {
            continue;
        }

        row_columns = split_csv_line(line,
                                     row_fields,
                                     header_columns,
                                     error,
                                     error_size);
        if (row_columns < 0) {
            fclose(file);
            return 0;
        }

        if (row_columns != header_columns) {
            fclose(file);
            snprintf(error, error_size, "Invalid row in table file: %s", path);
            return 0;
        }

        write_selected_fields(out, row_fields, selected_indexes, selected_count);
        row_count++;
    }

    fclose(file);
    fprintf(out, "Rows: %d\n", row_count);
    return 1;
}

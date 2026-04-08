#include "mini_sql.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct TableSchema {
    char **columns;
    size_t column_count;
} TableSchema;

typedef struct ResultRow {
    char **values;
    size_t count;
} ResultRow;

typedef struct ResultTable {
    ResultRow *rows;
    size_t row_count;
    size_t capacity;
} ResultTable;

static char *trim_in_place(char *text) {
    char *start = text;
    char *end;

    while (*start != '\0' && isspace((unsigned char) *start)) {
        start += 1;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char) end[-1])) {
        end -= 1;
    }
    *end = '\0';

    return start;
}

static char *normalize_table_name(const char *table_name) {
    char *normalized = msql_strdup(table_name);
    char *cursor;

    if (normalized == NULL) {
        return NULL;
    }

    for (cursor = normalized; *cursor != '\0'; ++cursor) {
        if (*cursor == '.') {
            *cursor = '/';
        }
    }

    return normalized;
}

static char *build_table_path(const char *db_path, const char *table_name, const char *extension) {
    char *normalized = normalize_table_name(table_name);
    size_t length;
    char *path;

    if (normalized == NULL) {
        return NULL;
    }

    length = strlen(db_path) + 1U + strlen(normalized) + strlen(extension);
    path = malloc(length + 1U);
    if (path == NULL) {
        free(normalized);
        return NULL;
    }

    snprintf(path, length + 1U, "%s/%s%s", db_path, normalized, extension);
    free(normalized);
    return path;
}

static bool ensure_parent_directories(const char *path, char *error_buf, size_t error_size) {
    char *mutable_path = msql_strdup(path);
    char *cursor;

    if (mutable_path == NULL) {
        set_error(error_buf, error_size, "out of memory while creating directories");
        return false;
    }

    for (cursor = mutable_path + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(mutable_path, 0755) != 0 && errno != EEXIST) {
                set_error(error_buf, error_size, "failed to create directory %s: %s", mutable_path, strerror(errno));
                free(mutable_path);
                return false;
            }
            *cursor = '/';
        }
    }

    free(mutable_path);
    return true;
}

static bool push_string(char ***items, size_t *count, size_t *capacity, const char *text,
                        char *error_buf, size_t error_size) {
    char **new_items;
    char *copy;
    size_t new_capacity;

    if (*count >= *capacity) {
        new_capacity = *capacity == 0U ? 8U : (*capacity * 2U);
        new_items = realloc(*items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            set_error(error_buf, error_size, "out of memory while processing CSV");
            return false;
        }
        *items = new_items;
        *capacity = new_capacity;
    }

    copy = msql_strdup(text);
    if (copy == NULL) {
        set_error(error_buf, error_size, "out of memory while processing CSV");
        return false;
    }

    (*items)[*count] = copy;
    *count += 1U;
    return true;
}

static bool parse_csv_line(const char *line, char ***out_fields, size_t *out_count,
                           char *error_buf, size_t error_size) {
    const char *cursor = line;
    char **fields = NULL;
    size_t count = 0U;
    size_t capacity = 0U;

    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
        bool quoted = false;
        size_t field_capacity = 32U;
        size_t field_length = 0U;
        char *field = malloc(field_capacity);

        if (field == NULL) {
            free_string_array(fields, count);
            set_error(error_buf, error_size, "out of memory while processing CSV");
            return false;
        }

        if (*cursor == '"') {
            quoted = true;
            cursor += 1;
            while (*cursor != '\0') {
                if (*cursor == '"') {
                    if (cursor[1] == '"') {
                        cursor += 2;
                        if (field_length + 1U >= field_capacity) {
                            char *new_field;

                            field_capacity *= 2U;
                            new_field = realloc(field, field_capacity);
                            if (new_field == NULL) {
                                free(field);
                                free_string_array(fields, count);
                                set_error(error_buf, error_size, "out of memory while processing CSV");
                                return false;
                            }
                            field = new_field;
                        }
                        field[field_length++] = '"';
                        continue;
                    }
                    cursor += 1;
                    break;
                }

                if (field_length + 1U >= field_capacity) {
                    char *new_field;

                    field_capacity *= 2U;
                    new_field = realloc(field, field_capacity);
                    if (new_field == NULL) {
                        free(field);
                        free_string_array(fields, count);
                        set_error(error_buf, error_size, "out of memory while processing CSV");
                        return false;
                    }
                    field = new_field;
                }

                field[field_length++] = *cursor;
                cursor += 1;
            }

            if (quoted && cursor[-1] != '"') {
                free(field);
                free_string_array(fields, count);
                set_error(error_buf, error_size, "malformed quoted CSV field");
                return false;
            }

            while (*cursor != '\0' && *cursor != ',' && *cursor != '\n' && *cursor != '\r') {
                if (!isspace((unsigned char) *cursor)) {
                    free(field);
                    free_string_array(fields, count);
                    set_error(error_buf, error_size, "unexpected character after quoted CSV field");
                    return false;
                }
                cursor += 1;
            }
        } else {
            while (*cursor != '\0' && *cursor != ',' && *cursor != '\n' && *cursor != '\r') {
                if (field_length + 1U >= field_capacity) {
                    char *new_field;

                    field_capacity *= 2U;
                    new_field = realloc(field, field_capacity);
                    if (new_field == NULL) {
                        free(field);
                        free_string_array(fields, count);
                        set_error(error_buf, error_size, "out of memory while processing CSV");
                        return false;
                    }
                    field = new_field;
                }
                field[field_length++] = *cursor;
                cursor += 1;
            }
        }

        field[field_length] = '\0';

        if (!push_string(&fields, &count, &capacity, field, error_buf, error_size)) {
            free(field);
            free_string_array(fields, count);
            return false;
        }
        free(field);

        if (*cursor == ',') {
            cursor += 1;
            continue;
        }
        break;
    }

    if (count == 0U) {
        if (!push_string(&fields, &count, &capacity, "", error_buf, error_size)) {
            free_string_array(fields, count);
            return false;
        }
    }

    *out_fields = fields;
    *out_count = count;
    return true;
}

static bool write_csv_row(FILE *file, char **fields, size_t field_count, char *error_buf, size_t error_size) {
    size_t i;
    size_t j;

    for (i = 0; i < field_count; ++i) {
        const char *field = fields[i];
        bool needs_quotes = strchr(field, ',') != NULL || strchr(field, '"') != NULL ||
                            strchr(field, '\n') != NULL || strchr(field, '\r') != NULL;

        if (needs_quotes) {
            if (fputc('"', file) == EOF) {
                set_error(error_buf, error_size, "failed to write CSV data");
                return false;
            }
            for (j = 0U; field[j] != '\0'; ++j) {
                if (field[j] == '"' && fputc('"', file) == EOF) {
                    set_error(error_buf, error_size, "failed to write CSV data");
                    return false;
                }
                if (fputc(field[j], file) == EOF) {
                    set_error(error_buf, error_size, "failed to write CSV data");
                    return false;
                }
            }
            if (fputc('"', file) == EOF) {
                set_error(error_buf, error_size, "failed to write CSV data");
                return false;
            }
        } else if (fputs(field, file) == EOF) {
            set_error(error_buf, error_size, "failed to write CSV data");
            return false;
        }

        if (i + 1U < field_count && fputc(',', file) == EOF) {
            set_error(error_buf, error_size, "failed to write CSV data");
            return false;
        }
    }

    if (fputc('\n', file) == EOF) {
        set_error(error_buf, error_size, "failed to write CSV data");
        return false;
    }

    return true;
}

static void free_table_schema(TableSchema *schema) {
    free_string_array(schema->columns, schema->column_count);
    schema->columns = NULL;
    schema->column_count = 0U;
}

static bool load_table_schema(const char *db_path, const char *table_name, TableSchema *schema,
                              char *error_buf, size_t error_size) {
    char *path = build_table_path(db_path, table_name, ".schema");
    char *contents = NULL;
    char *line;
    char **raw_fields = NULL;
    size_t raw_count = 0U;
    size_t i;

    if (path == NULL) {
        set_error(error_buf, error_size, "out of memory while loading schema");
        return false;
    }

    if (!read_file_all(path, &contents, error_buf, error_size)) {
        free(path);
        return false;
    }
    free(path);

    line = strtok(contents, "\n");
    while (line != NULL && trim_in_place(line)[0] == '\0') {
        line = strtok(NULL, "\n");
    }

    if (line == NULL) {
        free(contents);
        set_error(error_buf, error_size, "schema file is empty for table %s", table_name);
        return false;
    }

    if (!parse_csv_line(line, &raw_fields, &raw_count, error_buf, error_size)) {
        free(contents);
        return false;
    }

    for (i = 0; i < raw_count; ++i) {
        char *trimmed = trim_in_place(raw_fields[i]);
        char *copy = msql_strdup(trimmed);

        if (copy == NULL) {
            free(contents);
            free_string_array(raw_fields, raw_count);
            set_error(error_buf, error_size, "out of memory while loading schema");
            return false;
        }

        free(raw_fields[i]);
        raw_fields[i] = copy;
    }

    free(contents);
    schema->columns = raw_fields;
    schema->column_count = raw_count;
    return true;
}

static char *read_line_dynamic(FILE *file) {
    int ch;
    size_t capacity = 128U;
    size_t length = 0U;
    char *buffer = malloc(capacity);

    if (buffer == NULL) {
        return NULL;
    }

    while ((ch = fgetc(file)) != EOF) {
        if (length + 1U >= capacity) {
            char *new_buffer;

            capacity *= 2U;
            new_buffer = realloc(buffer, capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }

        buffer[length++] = (char) ch;
        if (ch == '\n') {
            break;
        }
    }

    if (length == 0U && ch == EOF) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static bool append_result_row(ResultTable *table, char **values, size_t count, char *error_buf, size_t error_size) {
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

static void free_result_table(ResultTable *table) {
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

static void print_result(FILE *output, char **headers, size_t column_count, const ResultTable *rows) {
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

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context,
                              char *error_buf, size_t error_size) {
    TableSchema schema = {0};
    char **row_values = NULL;
    bool *assigned = NULL;
    size_t i;
    char *data_path = NULL;
    FILE *data_file = NULL;
    FILE *output = context->output == NULL ? stdout : context->output;

    if (!load_table_schema(context->db_path, statement->table_name, &schema, error_buf, error_size)) {
        return false;
    }

    row_values = calloc(schema.column_count, sizeof(*row_values));
    assigned = calloc(schema.column_count, sizeof(*assigned));
    if (row_values == NULL || assigned == NULL) {
        free(row_values);
        free(assigned);
        free_table_schema(&schema);
        set_error(error_buf, error_size, "out of memory while preparing INSERT");
        return false;
    }

    for (i = 0; i < schema.column_count; ++i) {
        row_values[i] = msql_strdup("");
        if (row_values[i] == NULL) {
            free_string_array(row_values, i);
            free(assigned);
            free_table_schema(&schema);
            set_error(error_buf, error_size, "out of memory while preparing INSERT");
            return false;
        }
    }

    if (statement->column_count == 0U) {
        if (statement->value_count != schema.column_count) {
            free_string_array(row_values, schema.column_count);
            free(assigned);
            free_table_schema(&schema);
            set_error(error_buf, error_size, "INSERT value count does not match schema column count");
            return false;
        }

        for (i = 0; i < schema.column_count; ++i) {
            free(row_values[i]);
            row_values[i] = msql_strdup(statement->values[i]);
            if (row_values[i] == NULL) {
                free_string_array(row_values, schema.column_count);
                free(assigned);
                free_table_schema(&schema);
                set_error(error_buf, error_size, "out of memory while preparing INSERT");
                return false;
            }
        }
    } else {
        for (i = 0; i < statement->column_count; ++i) {
            int index = find_column_index(schema.columns, schema.column_count, statement->columns[i]);

            if (index < 0) {
                free_string_array(row_values, schema.column_count);
                free(assigned);
                free_table_schema(&schema);
                set_error(error_buf, error_size, "unknown column '%s' in INSERT", statement->columns[i]);
                return false;
            }
            if (assigned[index]) {
                free_string_array(row_values, schema.column_count);
                free(assigned);
                free_table_schema(&schema);
                set_error(error_buf, error_size, "duplicate column '%s' in INSERT", statement->columns[i]);
                return false;
            }

            free(row_values[index]);
            row_values[index] = msql_strdup(statement->values[i]);
            if (row_values[index] == NULL) {
                free_string_array(row_values, schema.column_count);
                free(assigned);
                free_table_schema(&schema);
                set_error(error_buf, error_size, "out of memory while preparing INSERT");
                return false;
            }

            assigned[index] = true;
        }
    }

    data_path = build_table_path(context->db_path, statement->table_name, ".data");
    if (data_path == NULL) {
        free_string_array(row_values, schema.column_count);
        free(assigned);
        free_table_schema(&schema);
        set_error(error_buf, error_size, "out of memory while resolving table path");
        return false;
    }

    if (!ensure_parent_directories(data_path, error_buf, error_size)) {
        free(data_path);
        free_string_array(row_values, schema.column_count);
        free(assigned);
        free_table_schema(&schema);
        return false;
    }

    data_file = fopen(data_path, "a");
    if (data_file == NULL) {
        set_error(error_buf, error_size, "failed to open data file %s: %s", data_path, strerror(errno));
        free(data_path);
        free_string_array(row_values, schema.column_count);
        free(assigned);
        free_table_schema(&schema);
        return false;
    }

    if (!write_csv_row(data_file, row_values, schema.column_count, error_buf, error_size)) {
        fclose(data_file);
        free(data_path);
        free_string_array(row_values, schema.column_count);
        free(assigned);
        free_table_schema(&schema);
        return false;
    }

    fclose(data_file);
    fprintf(output, "INSERT 1\n");

    free(data_path);
    free_string_array(row_values, schema.column_count);
    free(assigned);
    free_table_schema(&schema);
    return true;
}

bool execute_select_statement(const SelectStatement *statement, const ExecutionContext *context,
                              char *error_buf, size_t error_size) {
    TableSchema schema = {0};
    int *selected_indexes = NULL;
    size_t selected_count;
    char **headers = NULL;
    int where_index = -1;
    char *data_path = NULL;
    FILE *data_file = NULL;
    ResultTable results = {0};
    FILE *output = context->output == NULL ? stdout : context->output;
    size_t i;

    if (!load_table_schema(context->db_path, statement->table_name, &schema, error_buf, error_size)) {
        return false;
    }

    selected_count = statement->select_all ? schema.column_count : statement->column_count;
    selected_indexes = calloc(selected_count, sizeof(*selected_indexes));
    headers = calloc(selected_count, sizeof(*headers));
    if (selected_indexes == NULL || headers == NULL) {
        free(selected_indexes);
        free(headers);
        free_table_schema(&schema);
        set_error(error_buf, error_size, "out of memory while preparing SELECT");
        return false;
    }

    if (statement->select_all) {
        for (i = 0; i < selected_count; ++i) {
            selected_indexes[i] = (int) i;
            headers[i] = schema.columns[i];
        }
    } else {
        for (i = 0; i < selected_count; ++i) {
            int index = find_column_index(schema.columns, schema.column_count, statement->columns[i]);

            if (index < 0) {
                free(selected_indexes);
                free(headers);
                free_table_schema(&schema);
                set_error(error_buf, error_size, "unknown column '%s' in SELECT", statement->columns[i]);
                return false;
            }

            selected_indexes[i] = index;
            headers[i] = schema.columns[index];
        }
    }

    if (statement->has_where) {
        where_index = find_column_index(schema.columns, schema.column_count, statement->where_column);
        if (where_index < 0) {
            free(selected_indexes);
            free(headers);
            free_table_schema(&schema);
            set_error(error_buf, error_size, "unknown column '%s' in WHERE clause", statement->where_column);
            return false;
        }
    }

    data_path = build_table_path(context->db_path, statement->table_name, ".data");
    if (data_path == NULL) {
        free(selected_indexes);
        free(headers);
        free_table_schema(&schema);
        set_error(error_buf, error_size, "out of memory while resolving table path");
        return false;
    }

    data_file = fopen(data_path, "r");
    if (data_file != NULL) {
        char *line;

        while ((line = read_line_dynamic(data_file)) != NULL) {
            char *trimmed = trim_in_place(line);

            if (*trimmed != '\0') {
                char **fields = NULL;
                size_t field_count = 0U;

                if (!parse_csv_line(trimmed, &fields, &field_count, error_buf, error_size)) {
                    free(line);
                    fclose(data_file);
                    free(data_path);
                    free(selected_indexes);
                    free(headers);
                    free_result_table(&results);
                    free_table_schema(&schema);
                    return false;
                }

                if (field_count != schema.column_count) {
                    free(line);
                    fclose(data_file);
                    free(data_path);
                    free(selected_indexes);
                    free(headers);
                    free_string_array(fields, field_count);
                    free_result_table(&results);
                    free_table_schema(&schema);
                    set_error(error_buf, error_size, "corrupted row in table '%s': expected %zu columns but got %zu",
                              statement->table_name, schema.column_count, field_count);
                    return false;
                }

                if (!statement->has_where || strcmp(fields[where_index], statement->where_value) == 0) {
                    char **selected_values = calloc(selected_count, sizeof(*selected_values));

                    if (selected_values == NULL) {
                        free(line);
                        fclose(data_file);
                        free(data_path);
                        free(selected_indexes);
                        free(headers);
                        free_string_array(fields, field_count);
                        free_result_table(&results);
                        free_table_schema(&schema);
                        set_error(error_buf, error_size, "out of memory while building result set");
                        return false;
                    }

                    for (i = 0; i < selected_count; ++i) {
                        selected_values[i] = msql_strdup(fields[selected_indexes[i]]);
                        if (selected_values[i] == NULL) {
                            free(line);
                            fclose(data_file);
                            free(data_path);
                            free(selected_indexes);
                            free(headers);
                            free_string_array(fields, field_count);
                            free_string_array(selected_values, i);
                            free(selected_values);
                            free_result_table(&results);
                            free_table_schema(&schema);
                            set_error(error_buf, error_size, "out of memory while building result set");
                            return false;
                        }
                    }

                    if (!append_result_row(&results, selected_values, selected_count, error_buf, error_size)) {
                        free(line);
                        fclose(data_file);
                        free(data_path);
                        free(selected_indexes);
                        free(headers);
                        free_string_array(fields, field_count);
                        free_string_array(selected_values, selected_count);
                        free(selected_values);
                        free_result_table(&results);
                        free_table_schema(&schema);
                        return false;
                    }
                }

                free_string_array(fields, field_count);
            }

            free(line);
        }

        fclose(data_file);
    } else if (errno != ENOENT) {
        set_error(error_buf, error_size, "failed to open data file %s: %s", data_path, strerror(errno));
        free(data_path);
        free(selected_indexes);
        free(headers);
        free_result_table(&results);
        free_table_schema(&schema);
        return false;
    }

    print_result(output, headers, selected_count, &results);

    free(data_path);
    free(selected_indexes);
    free(headers);
    free_result_table(&results);
    free_table_schema(&schema);
    return true;
}


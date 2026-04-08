#include "table_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *trim_in_place(char *text) {
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

char *build_table_path(const char *db_path, const char *table_name, const char *extension) {
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

bool ensure_parent_directories(const char *path, char *error_buf, size_t error_size) {
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

bool parse_csv_line(const char *line, char ***out_fields, size_t *out_count,
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

bool write_csv_row(FILE *file, char **fields, size_t field_count, char *error_buf, size_t error_size) {
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

void free_table_schema(TableSchema *schema) {
    free_string_array(schema->columns, schema->column_count);
    schema->columns = NULL;
    schema->column_count = 0U;
}

bool load_table_schema(const char *db_path, const char *table_name, TableSchema *schema,
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

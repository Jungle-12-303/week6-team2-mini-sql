#include "mini_sql.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

char *msql_strdup(const char *text) {
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1);
    return copy;
}

char *read_stream_line(FILE *stream) {
    int ch;
    char *buffer = NULL;
    size_t length = 0U;
    size_t capacity = 0U;

    if (stream == NULL) {
        return NULL;
    }

    while ((ch = fgetc(stream)) != EOF) {
        char *new_buffer;
        size_t new_capacity;

        if (length + 2U <= capacity) {
            buffer[length++] = (char) ch;
            if (ch == '\n') {
                break;
            }
            continue;
        }

        new_capacity = capacity == 0U ? 128U : capacity * 2U;
        new_buffer = realloc(buffer, new_capacity);
        if (new_buffer == NULL) {
            free(buffer);
            return NULL;
        }

        buffer = new_buffer;
        capacity = new_capacity;
        buffer[length++] = (char) ch;
        if (ch == '\n') {
            break;
        }
    }

    if (length == 0U && ch == EOF) {
        free(buffer);
        return NULL;
    }

    if (buffer == NULL) {
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

void set_error(char *error_buf, size_t error_size, const char *fmt, ...) {
    va_list args;

    if (error_buf == NULL || error_size == 0U) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(error_buf, error_size, fmt, args);
    va_end(args);
}

bool read_file_all(const char *path, char **out_contents, char *error_buf, size_t error_size) {
    FILE *file;
    long file_size;
    size_t read_size;
    char *buffer;

    *out_contents = NULL;

    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error_buf, error_size, "failed to open file: %s", path);
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        set_error(error_buf, error_size, "failed to seek file: %s", path);
        return false;
    }

    file_size = ftell(file);
    if (file_size < 0L) {
        fclose(file);
        set_error(error_buf, error_size, "failed to measure file: %s", path);
        return false;
    }

    rewind(file);

    buffer = malloc((size_t) file_size + 1U);
    if (buffer == NULL) {
        fclose(file);
        set_error(error_buf, error_size, "out of memory while reading: %s", path);
        return false;
    }

    read_size = fread(buffer, 1U, (size_t) file_size, file);
    if (read_size != (size_t) file_size && ferror(file) != 0) {
        fclose(file);
        free(buffer);
        set_error(error_buf, error_size, "failed to read file: %s", path);
        return false;
    }

    buffer[read_size] = '\0';
    fclose(file);
    *out_contents = buffer;
    return true;
}

void free_string_array(char **items, size_t count) {
    size_t i;

    if (items == NULL) {
        return;
    }

    for (i = 0; i < count; ++i) {
        free(items[i]);
    }

    free(items);
}

bool strings_equal_ci(const char *left, const char *right) {
    unsigned char a;
    unsigned char b;

    if (left == NULL || right == NULL) {
        return false;
    }

    while (*left != '\0' && *right != '\0') {
        a = (unsigned char) *left;
        b = (unsigned char) *right;
        if (tolower(a) != tolower(b)) {
            return false;
        }
        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

int find_column_index(char **columns, size_t column_count, const char *target) {
    size_t i;

    for (i = 0; i < column_count; ++i) {
        if (strings_equal_ci(columns[i], target)) {
            return (int) i;
        }
    }

    return -1;
}

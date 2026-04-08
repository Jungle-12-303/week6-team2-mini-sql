#include "mini_sql.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* malloc 기반 문자열 복사 유틸리티다. 호출한 쪽이 free 해야 한다. */
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

/*
 * FILE* 에서 한 줄을 길이 제한 없이 읽어온다.
 * 개행 문자가 있으면 함께 포함해서 반환하고, EOF 면 NULL 을 반환한다.
 */
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

/* printf 스타일로 에러 메시지를 공통 버퍼에 기록한다. */
void set_error(ErrorContext *err, const char *fmt, ...) {
    va_list args;

    if (err == NULL) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(err->buf, sizeof(err->buf), fmt, args);
    va_end(args);
}

/*
 * 파일 전체를 한 번에 메모리로 읽어오는 유틸리티다.
 * SQL 파일, schema 파일처럼 크기가 작은 텍스트 파일을 단순하게 처리하려는 목적에 맞춰 만들었다.
 */
bool read_file_all(const char *path, char **out_contents, ErrorContext *err) {
    FILE *file;
    long file_size;
    size_t read_size;
    char *buffer;

    *out_contents = NULL;

    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(err, "파일을 열지 못했습니다: %s", path);
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        set_error(err, "파일 위치를 이동하지 못했습니다: %s", path);
        return false;
    }

    file_size = ftell(file);
    if (file_size < 0L) {
        fclose(file);
        set_error(err, "파일 크기를 확인하지 못했습니다: %s", path);
        return false;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        set_error(err, "파일 시작 위치로 되돌리지 못했습니다: %s", path);
        return false;
    }

    buffer = malloc((size_t) file_size + 1U);
    if (buffer == NULL) {
        fclose(file);
        set_error(err, "파일을 읽는 중 메모리가 부족합니다: %s", path);
        return false;
    }

    read_size = fread(buffer, 1U, (size_t) file_size, file);
    if (read_size != (size_t) file_size && ferror(file) != 0) {
        fclose(file);
        free(buffer);
        set_error(err, "파일을 읽지 못했습니다: %s", path);
        return false;
    }

    buffer[read_size] = '\0';
    fclose(file);
    *out_contents = buffer;
    return true;
}

/*
 * text 를 복사해 동적 문자열 배열의 끝에 추가한다.
 * 용량이 부족하면 2배로 늘리고, 복사에 실패하면 에러를 기록한 뒤 false 를 반환한다.
 */
bool msql_string_array_push(char ***items, size_t *count, size_t *capacity,
                            const char *text, ErrorContext *err) {
    char **new_items;
    char *copy;
    size_t new_capacity;

    if (*count >= *capacity) {
        new_capacity = *capacity == 0U ? 8U : (*capacity * 2U);
        new_items = realloc(*items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            set_error(err, "메모리가 부족합니다");
            return false;
        }
        *items = new_items;
        *capacity = new_capacity;
    }

    copy = msql_strdup(text);
    if (copy == NULL) {
        set_error(err, "메모리가 부족합니다");
        return false;
    }

    (*items)[*count] = copy;
    *count += 1U;
    return true;
}

/*
 * 이미 소유권을 가진 text 를 동적 문자열 배열의 끝에 추가한다.
 * 용량 확보에 실패하면 text 를 free 한 뒤 false 를 반환한다.
 */
bool msql_string_array_push_owned(char ***items, size_t *count, size_t *capacity,
                                  char *text, ErrorContext *err) {
    char **new_items;
    size_t new_capacity;

    if (*count >= *capacity) {
        new_capacity = *capacity == 0U ? 8U : (*capacity * 2U);
        new_items = realloc(*items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            free(text);
            set_error(err, "메모리가 부족합니다");
            return false;
        }
        *items = new_items;
        *capacity = new_capacity;
    }

    (*items)[*count] = text;
    *count += 1U;
    return true;
}

/* char* 배열을 순회하며 각 원소와 배열 자체를 함께 해제한다. */
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

/* SQL 키워드와 컬럼 이름 비교에 쓰는 대소문자 무시 문자열 비교 함수다. */
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

/* 컬럼 이름 배열에서 target 의 위치를 찾아 인덱스로 반환한다. 없으면 -1 이다. */
int find_column_index(char **columns, size_t column_count, const char *target) {
    size_t i;

    for (i = 0; i < column_count; ++i) {
        if (strings_equal_ci(columns[i], target)) {
            return (int) i;
        }
    }

    return -1;
}

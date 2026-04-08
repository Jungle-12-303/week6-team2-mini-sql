#include "storage/csv_codec.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * csv_codec.c 는 CSV 형식의 직렬화/역직렬화를 전담한다.
 *
 * parse_csv_line 은 한 줄을 읽어 필드 배열로 복원하고,
 * write_csv_row  는 필드 배열을 한 줄 CSV 로 직렬화한다.
 * 두 함수 모두 RFC 4180 기반의 큰따옴표 이스케이프를 지원한다.
 */

/* 같은 버퍼 안에서 앞뒤 공백을 제거하고, 잘린 시작 위치를 반환한다. */
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

/*
 * CSV 한 줄을 필드 배열로 복원한다.
 * 따옴표와 쉼표 escape 를 해석해서 SELECT/DELETE 가 다루기 쉬운 평면 문자열 배열로 만든다.
 */
bool parse_csv_line(const char *line, char ***out_fields, size_t *out_count,
                    ErrorContext *err) {
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
            set_error(err, "CSV를 처리하는 중 메모리가 부족합니다");
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
                                set_error(err, "CSV를 처리하는 중 메모리가 부족합니다");
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
                        set_error(err, "CSV를 처리하는 중 메모리가 부족합니다");
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
                set_error(err, "따옴표로 감싼 CSV 필드 형식이 올바르지 않습니다");
                return false;
            }

            while (*cursor != '\0' && *cursor != ',' && *cursor != '\n' && *cursor != '\r') {
                if (!isspace((unsigned char) *cursor)) {
                    free(field);
                    free_string_array(fields, count);
                    set_error(err, "따옴표로 감싼 CSV 필드 뒤에 예상하지 못한 문자가 있습니다");
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
                        set_error(err, "CSV를 처리하는 중 메모리가 부족합니다");
                        return false;
                    }
                    field = new_field;
                }
                field[field_length++] = *cursor;
                cursor += 1;
            }
        }

        field[field_length] = '\0';

        if (!msql_string_array_push_owned(&fields, &count, &capacity, field, err)) {
            free_string_array(fields, count);
            return false;
        }

        if (*cursor == ',') {
            cursor += 1;
            continue;
        }
        break;
    }

    if (count == 0U) {
        if (!msql_string_array_push(&fields, &count, &capacity, "", err)) {
            free_string_array(fields, count);
            return false;
        }
    }

    *out_fields = fields;
    *out_count = count;
    return true;
}

/* 필드 배열을 CSV 규칙에 맞춰 한 줄로 저장한다. */
bool write_csv_row(FILE *file, char **fields, size_t field_count, ErrorContext *err) {
    size_t i;
    size_t j;

    for (i = 0; i < field_count; ++i) {
        const char *field = fields[i];
        bool needs_quotes = strchr(field, ',') != NULL || strchr(field, '"') != NULL ||
                            strchr(field, '\n') != NULL || strchr(field, '\r') != NULL;

        if (needs_quotes) {
            if (fputc('"', file) == EOF) {
                set_error(err, "CSV 데이터를 쓰지 못했습니다");
                return false;
            }
            for (j = 0U; field[j] != '\0'; ++j) {
                if (field[j] == '"' && fputc('"', file) == EOF) {
                    set_error(err, "CSV 데이터를 쓰지 못했습니다");
                    return false;
                }
                if (fputc(field[j], file) == EOF) {
                    set_error(err, "CSV 데이터를 쓰지 못했습니다");
                    return false;
                }
            }
            if (fputc('"', file) == EOF) {
                set_error(err, "CSV 데이터를 쓰지 못했습니다");
                return false;
            }
        } else if (fputs(field, file) == EOF) {
            set_error(err, "CSV 데이터를 쓰지 못했습니다");
            return false;
        }

        if (i + 1U < field_count && fputc(',', file) == EOF) {
            set_error(err, "CSV 데이터를 쓰지 못했습니다");
            return false;
        }
    }

    if (fputc('\n', file) == EOF) {
        set_error(err, "CSV 데이터를 쓰지 못했습니다");
        return false;
    }

    return true;
}

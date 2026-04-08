#include "result/result_table.h"

#include <stdlib.h>
#include <string.h>

/* SELECT 결과 한 행을 ResultTable 뒤에 붙인다. */
bool append_result_row(ResultTable *table, char **values, size_t count, ErrorContext *err) {
    ResultRow *new_rows;
    size_t new_capacity;

    if (table->row_count >= table->capacity) {
        new_capacity = table->capacity == 0U ? 8U : table->capacity * 2U;
        new_rows = realloc(table->rows, new_capacity * sizeof(*new_rows));
        if (new_rows == NULL) {
            set_error(err, "조회 결과를 만드는 중 메모리가 부족합니다");
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

/* ResultTable 이 소유한 각 행의 문자열 배열까지 함께 해제한다. */
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

/* ASCII 표 위아래 경계를 그리는 공통 출력 함수다. */
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

/* 헤더 행과 데이터 행을 같은 형식으로 출력하기 위한 공통 함수다. */
static void print_row(FILE *output, char **values, const size_t *widths, size_t column_count) {
    size_t i;

    for (i = 0; i < column_count; ++i) {
        fprintf(output, "| %-*s ", (int) widths[i], values[i]);
    }
    fputs("|\n", output);
}

/* 기본 ASCII 표 포맷터 구현이다. */
static void ascii_formatter_print(FILE *output, char **headers, size_t column_count, const ResultTable *rows) {
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
    fprintf(output, "(%zu개 행)\n", rows->row_count);

    free(widths);
}

static const ResultFormatter ASCII_RESULT_FORMATTER = {
    ascii_formatter_print
};

/* 기본 ASCII 표 포맷터를 내부적으로만 꺼낸다. */
static const ResultFormatter *ascii_result_formatter(void) {
    return &ASCII_RESULT_FORMATTER;
}

/* formatter 가 NULL 이어도 기본 ASCII 포맷터로 결과를 출력한다. */
void result_formatter_print(const ResultFormatter *formatter, FILE *output,
                            char **headers, size_t column_count, const ResultTable *rows) {
    const ResultFormatter *resolved = formatter != NULL ? formatter : ascii_result_formatter();
    resolved->print(output, headers, column_count, rows);
}

#include "history.h"

#include <stdlib.h>
#include <string.h>

/*
 * history.c 는 SQL CLI 명령 히스토리 기능을 담당한다.
 *
 * history_push 로 항목을 추가하고,
 * history_get  으로 인덱스 기반 탐색을 할 수 있다.
 * 중복 연속 항목과 빈 줄은 저장하지 않는다.
 */

/* 히스토리가 소유한 문자열 배열 메모리를 모두 해제한다. */
void history_free(LineHistory *history) {
    free_string_array(history->items, history->count);
    history->items    = NULL;
    history->count    = 0U;
    history->capacity = 0U;
}

/* 빈 줄과 직전 중복 명령을 제외하고 히스토리에 저장한다. */
bool history_push(LineHistory *history, const char *line, ErrorContext *err) {
    char **new_items;
    char *copy;
    size_t new_capacity;

    if (line[0] == '\0') {
        return true;
    }

    if (history->count > 0U && strcmp(history->items[history->count - 1U], line) == 0) {
        return true;
    }

    if (history->count >= history->capacity) {
        new_capacity = history->capacity == 0U ? 16U : history->capacity * 2U;
        new_items = realloc(history->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            set_error(err, "명령 히스토리를 저장하는 중 메모리가 부족합니다");
            return false;
        }
        history->items    = new_items;
        history->capacity = new_capacity;
    }

    copy = msql_strdup(line);
    if (copy == NULL) {
        set_error(err, "명령 히스토리를 저장하는 중 메모리가 부족합니다");
        return false;
    }

    history->items[history->count] = copy;
    history->count += 1U;
    return true;
}

/* index 위치 항목을 반환한다. 범위 초과이면 NULL 을 반환한다. */
const char *history_get(const LineHistory *history, size_t index) {
    if (index >= history->count) {
        return NULL;
    }
    return history->items[index];
}

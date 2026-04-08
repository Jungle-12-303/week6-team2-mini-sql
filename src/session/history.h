#ifndef HISTORY_H
#define HISTORY_H

#include "mini_sql.h"

/*
 * history.h / history.c 는 SQL CLI 의 명령 히스토리 저장/탐색을 전담한다.
 * sql_cli.c 는 이 인터페이스만 보고 히스토리를 조작할 수 있다.
 */

/* 위/아래 방향키 탐색을 위해 이전 입력을 저장하는 구조체다. */
typedef struct LineHistory {
    char **items;
    size_t count;
    size_t capacity;
} LineHistory;

/* 히스토리 전체를 정리한다. */
void history_free(LineHistory *history);
/*
 * 새 입력을 히스토리에 추가한다.
 * 빈 줄과 직전과 동일한 명령은 건너뛴다.
 */
bool history_push(LineHistory *history, const char *line, ErrorContext *err);
/* index 위치의 히스토리 항목을 반환한다. 범위 초과이면 NULL 을 반환한다. */
const char *history_get(const LineHistory *history, size_t index);

#endif

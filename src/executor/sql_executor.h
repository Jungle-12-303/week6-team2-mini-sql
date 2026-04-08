#ifndef SQL_EXECUTOR_H
#define SQL_EXECUTOR_H

#include "mini_sql.h"

/* StatementType 하나를 받아 실제 실행 함수로 연결하는 함수 포인터 타입이다. */
typedef bool (*SqlStatementHandlerFn)(const Statement *statement, const ExecutionContext *context,
                                      ErrorContext *err);

/* 특정 StatementType 과 해당 처리 함수를 묶는 테이블 엔트리다. */
typedef struct SqlStatementHandlerEntry {
    StatementType type;
    SqlStatementHandlerFn handler;
} SqlStatementHandlerEntry;

/*
 * SqlExecutor 는 StatementList 를 순서대로 실행하는 계층이다.
 * 어떤 문장을 어떤 statement 파일로 위임할지는 handler table 이 결정한다.
 */
typedef struct SqlExecutor {
    const SqlStatementHandlerEntry *handlers;
    size_t handler_count;
} SqlExecutor;

void sql_executor_init(SqlExecutor *executor);
bool sql_executor_execute(const SqlExecutor *executor, const StatementList *statements,
                          const ExecutionContext *context, ErrorContext *err);

#endif

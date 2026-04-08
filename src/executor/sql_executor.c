#include "executor/sql_executor.h"

#include "executor/statements/sql_statement_handlers.h"

static bool handle_insert_statement(const Statement *statement, const ExecutionContext *context,
                                    ErrorContext *err) {
    /* INSERT AST 를 INSERT 실행 함수에 그대로 전달한다. */
    return execute_insert_statement(&statement->as.insert_stmt, context, err);
}

static bool handle_select_statement(const Statement *statement, const ExecutionContext *context,
                                    ErrorContext *err) {
    /* SELECT AST 를 SELECT 실행 함수에 그대로 전달한다. */
    return execute_select_statement(&statement->as.select_stmt, context, err);
}

static bool handle_create_table_statement(const Statement *statement, const ExecutionContext *context,
                                          ErrorContext *err) {
    /* CREATE TABLE AST 를 CREATE 실행 함수에 그대로 전달한다. */
    return execute_create_table_statement(&statement->as.create_table_stmt, context, err);
}

static bool handle_drop_table_statement(const Statement *statement, const ExecutionContext *context,
                                        ErrorContext *err) {
    /* DROP TABLE AST 를 DROP 실행 함수에 그대로 전달한다. */
    return execute_drop_table_statement(&statement->as.drop_table_stmt, context, err);
}

static bool handle_delete_statement(const Statement *statement, const ExecutionContext *context,
                                    ErrorContext *err) {
    /* DELETE AST 를 DELETE 실행 함수에 그대로 전달한다. */
    return execute_delete_statement(&statement->as.delete_stmt, context, err);
}

static const SqlStatementHandlerEntry DEFAULT_SQL_STATEMENT_HANDLERS[] = {
    {STATEMENT_INSERT, handle_insert_statement},
    {STATEMENT_SELECT, handle_select_statement},
    {STATEMENT_CREATE_TABLE, handle_create_table_statement},
    {STATEMENT_DROP_TABLE, handle_drop_table_statement},
    {STATEMENT_DELETE, handle_delete_statement}
};

void sql_executor_init(SqlExecutor *executor) {
    /* 기본 문장 핸들러 테이블을 실행기에 연결한다. */
    executor->handlers = DEFAULT_SQL_STATEMENT_HANDLERS;
    /* 핸들러 개수도 함께 기록해 선형 탐색 범위를 정한다. */
    executor->handler_count = sizeof(DEFAULT_SQL_STATEMENT_HANDLERS) / sizeof(DEFAULT_SQL_STATEMENT_HANDLERS[0]);
}

static SqlStatementHandlerFn find_sql_statement_handler(const SqlExecutor *executor, StatementType type) {
    /* 핸들러 배열을 순회할 인덱스다. */
    size_t i;

    /* 등록된 핸들러를 앞에서부터 하나씩 본다. */
    for (i = 0; i < executor->handler_count; ++i) {
        /* 현재 핸들러가 찾는 문장 타입과 같으면 그 함수를 반환한다. */
        if (executor->handlers[i].type == type) {
            return executor->handlers[i].handler;
        }
    }

    /* 끝까지 못 찾았으면 지원하지 않는 문장 타입이다. */
    return NULL;
}

static bool execute_single_statement(const SqlExecutor *executor, const Statement *statement,
                                     const ExecutionContext *context, ErrorContext *err) {
    /* 현재 문장 타입에 맞는 실제 실행 함수를 찾는다. */
    SqlStatementHandlerFn handler = find_sql_statement_handler(executor, statement->type);

    /* 핸들러가 없으면 지원하지 않는 문장이다. */
    if (handler == NULL) {
        set_error(err, "알 수 없는 SQL 문장 종류입니다");
        return false;
    }

    /* 찾은 핸들러로 문장 하나를 실행한다. */
    return handler(statement, context, err);
}

bool sql_executor_execute(const SqlExecutor *executor, const StatementList *statements,
                          const ExecutionContext *context, ErrorContext *err) {
    /* 여러 문장을 순서대로 실행하기 위한 인덱스다. */
    size_t i;

    /* AST 목록을 앞에서부터 하나씩 실행한다. */
    for (i = 0; i < statements->count; ++i) {
        /* 문장 하나라도 실패하면 즉시 중단하고 false 를 반환한다. */
        if (!execute_single_statement(executor, &statements->items[i], context, err)) {
            return false;
        }
    }

    /* 모든 문장이 성공했으면 true 를 반환한다. */
    return true;
}

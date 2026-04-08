#include "sql_processor.h"

void sql_processor_init(SqlProcessor *processor, const StatementExecutor *executor) {
    processor->statement_executor = executor;
}

bool sql_processor_process(const SqlProcessor *processor, const char *sql, const ExecutionContext *context,
                           char *error_buf, size_t error_size) {
    TokenList tokens = {0};
    StatementList statements = {0};
    bool ok = false;

    /* 1. 원본 SQL 문자열을 토큰 목록으로 분해한다. */
    if (!tokenize_sql(sql, &tokens, error_buf, error_size)) {
        return false;
    }

    /* 2. 토큰 목록을 INSERT / SELECT AST 목록으로 변환한다. */
    if (!parse_tokens(&tokens, &statements, error_buf, error_size)) {
        free_token_list(&tokens);
        return false;
    }

    /* 3. AST를 순서대로 실행해서 저장 엔진에 작업을 위임한다. */
    ok = statement_executor_execute(processor->statement_executor, &statements, context, error_buf, error_size);

    /* 4. 실행이 끝나면 토큰과 AST 메모리를 공통 정리 루틴으로 해제한다. */
    free_statement_list(&statements);
    free_token_list(&tokens);
    return ok;
}

bool execute_statements(const StatementList *statements, const ExecutionContext *context,
                        char *error_buf, size_t error_size) {
    StatementExecutor executor;

    statement_executor_init(&executor);
    return statement_executor_execute(&executor, statements, context, error_buf, error_size);
}

bool process_sql(const char *sql, const ExecutionContext *context, char *error_buf, size_t error_size) {
    StatementExecutor executor;
    SqlProcessor processor;

    statement_executor_init(&executor);
    sql_processor_init(&processor, &executor);
    return sql_processor_process(&processor, sql, context, error_buf, error_size);
}

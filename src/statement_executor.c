#include "statement_executor.h"

static bool handle_insert_statement(const Statement *statement, const ExecutionContext *context,
                                    char *error_buf, size_t error_size) {
    return execute_insert_statement(&statement->as.insert_stmt, context, error_buf, error_size);
}

static bool handle_select_statement(const Statement *statement, const ExecutionContext *context,
                                    char *error_buf, size_t error_size) {
    return execute_select_statement(&statement->as.select_stmt, context, error_buf, error_size);
}

static const StatementHandlerEntry DEFAULT_STATEMENT_HANDLERS[] = {
    {STATEMENT_INSERT, handle_insert_statement},
    {STATEMENT_SELECT, handle_select_statement}
};

void statement_executor_init(StatementExecutor *executor) {
    executor->handlers = DEFAULT_STATEMENT_HANDLERS;
    executor->handler_count = sizeof(DEFAULT_STATEMENT_HANDLERS) / sizeof(DEFAULT_STATEMENT_HANDLERS[0]);
}

static StatementHandlerFn find_statement_handler(const StatementExecutor *executor, StatementType type) {
    size_t i;

    for (i = 0; i < executor->handler_count; ++i) {
        if (executor->handlers[i].type == type) {
            return executor->handlers[i].handler;
        }
    }

    return NULL;
}

bool statement_executor_execute(const StatementExecutor *executor, const StatementList *statements,
                                const ExecutionContext *context, char *error_buf, size_t error_size) {
    size_t i;

    for (i = 0; i < statements->count; ++i) {
        const Statement *statement = &statements->items[i];
        StatementHandlerFn handler = find_statement_handler(executor, statement->type);

        if (handler == NULL) {
            set_error(error_buf, error_size, "unknown statement type");
            return false;
        }

        if (!handler(statement, context, error_buf, error_size)) {
            return false;
        }
    }

    return true;
}

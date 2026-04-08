#ifndef STATEMENT_EXECUTOR_H
#define STATEMENT_EXECUTOR_H

#include "mini_sql.h"

typedef bool (*StatementHandlerFn)(const Statement *statement, const ExecutionContext *context,
                                   char *error_buf, size_t error_size);

typedef struct StatementHandlerEntry {
    StatementType type;
    StatementHandlerFn handler;
} StatementHandlerEntry;

typedef struct StatementExecutor {
    const StatementHandlerEntry *handlers;
    size_t handler_count;
} StatementExecutor;

void statement_executor_init(StatementExecutor *executor);
bool statement_executor_execute(const StatementExecutor *executor, const StatementList *statements,
                                const ExecutionContext *context, char *error_buf, size_t error_size);

#endif

#ifndef SQL_PROCESSOR_H
#define SQL_PROCESSOR_H

#include "mini_sql.h"
#include "statement_executor.h"

typedef struct SqlProcessor {
    const StatementExecutor *statement_executor;
} SqlProcessor;

void sql_processor_init(SqlProcessor *processor, const StatementExecutor *executor);
bool sql_processor_process(const SqlProcessor *processor, const char *sql, const ExecutionContext *context,
                           char *error_buf, size_t error_size);

#endif

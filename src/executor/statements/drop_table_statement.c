#include "executor/statements/sql_statement_handlers.h"

#include "executor/statements/sql_statement_support.h"
#include "storage/storage_engine.h"

bool execute_drop_table_statement(const DropTableStatement *statement, const ExecutionContext *context,
                                  ErrorContext *err) {
    if (!storage_engine_drop_table(context->storage_engine, statement->table_name, err)) {
        return false;
    }

    sql_executor_print_status(context, "DROP TABLE");
    return true;
}

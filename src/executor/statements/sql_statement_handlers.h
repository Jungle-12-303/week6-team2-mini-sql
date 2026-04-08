#ifndef SQL_STATEMENT_HANDLERS_H
#define SQL_STATEMENT_HANDLERS_H

#include "mini_sql.h"

bool execute_insert_statement(const InsertStatement *statement, const ExecutionContext *context, ErrorContext *err);
bool execute_select_statement(const SelectStatement *statement, const ExecutionContext *context, ErrorContext *err);
bool execute_create_table_statement(const CreateTableStatement *statement, const ExecutionContext *context, ErrorContext *err);
bool execute_drop_table_statement(const DropTableStatement *statement, const ExecutionContext *context, ErrorContext *err);
bool execute_delete_statement(const DeleteStatement *statement, const ExecutionContext *context, ErrorContext *err);

#endif

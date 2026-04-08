#include "mini_sql.h"

bool execute_statements(const StatementList *statements, const ExecutionContext *context,
                        char *error_buf, size_t error_size) {
    size_t i;

    for (i = 0; i < statements->count; ++i) {
        const Statement *statement = &statements->items[i];

        if (statement->type == STATEMENT_INSERT) {
            if (!execute_insert_statement(&statement->as.insert_stmt, context, error_buf, error_size)) {
                return false;
            }
        } else if (statement->type == STATEMENT_SELECT) {
            if (!execute_select_statement(&statement->as.select_stmt, context, error_buf, error_size)) {
                return false;
            }
        } else {
            set_error(error_buf, error_size, "unknown statement type");
            return false;
        }
    }

    return true;
}

bool process_sql(const char *sql, const ExecutionContext *context, char *error_buf, size_t error_size) {
    TokenList tokens = {0};
    StatementList statements = {0};
    bool ok = false;

    if (!tokenize_sql(sql, &tokens, error_buf, error_size)) {
        return false;
    }

    if (!parse_tokens(&tokens, &statements, error_buf, error_size)) {
        free_token_list(&tokens);
        return false;
    }

    ok = execute_statements(&statements, context, error_buf, error_size);
    free_statement_list(&statements);
    free_token_list(&tokens);
    return ok;
}


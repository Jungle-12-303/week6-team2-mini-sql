#include "executor.h"

#include "storage.h"

#include <stdio.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

int execute_statement(const Statement *stmt,
                      const char *data_dir,
                      FILE *out,
                      char *error,
                      size_t error_size) {
    if (stmt == NULL || data_dir == NULL || out == NULL) {
        set_error(error, error_size, "Internal executor error");
        return 0;
    }

    switch (stmt->type) {
        case STATEMENT_INSERT:
            if (!storage_append_row(data_dir, stmt, error, error_size)) {
                return 0;
            }
            fprintf(out, "Inserted 1 row into %s\n", stmt->table_name);
            return 1;

        case STATEMENT_SELECT:
            return storage_select_all(data_dir, stmt, out, error, error_size);

        default:
            set_error(error, error_size, "Unknown statement type");
            return 0;
    }
}
